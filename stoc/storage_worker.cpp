
//
// Created by Haoyu Huang on 5/9/20.
// Copyright (c) 2020 University of Southern California. All rights reserved.
//

#include "storage_worker.h"

#include <fmt/core.h>
#include <semaphore.h>
#include "db/compaction.h"
#include "db/table_cache.h"

#include "db/filename.h"
#include "novalsm/rdma_server.h"
#include "ltc/stoc_client_impl.h"
#include "common/nova_config.h"
#include "db/version_set.h"

namespace nova {
    StorageWorker::StorageWorker(
            leveldb::StocPersistentFileManager *stoc_file_manager,
            std::vector<RDMAServerImpl *> &rdma_servers,
            const leveldb::Comparator *user_comparator,
            const leveldb::Options &options,
            leveldb::StoCClient *client,
            leveldb::MemManager *mem_manager,
            uint64_t thread_id, leveldb::Env *env)
            : stoc_file_manager_(
            stoc_file_manager), rdma_servers_(rdma_servers), user_comparator_(
            user_comparator), options_(options), icmp_(user_comparator),
              client_(client),
              mem_manager_(mem_manager), thread_id_(thread_id), env_(env) {
        stat_tasks_ = 0;
        stat_read_bytes_ = 0;
        stat_write_bytes_ = 0;
        sem_init(&sem_, 0, 0);
    }

    void StorageWorker::AddTask(
            const nova::StorageTask &task) {
        mutex_.lock();
        stat_tasks_ += 1;
        queue_.push_back(task);
        mutex_.unlock();

        sem_post(&sem_);
    }

    void StorageWorker::Start() {
        NOVA_LOG(DEBUG) << "CC server worker started";

        nova::NovaConfig::config->add_tid_mapping();

        while (is_running_) {
            sem_wait(&sem_);

            std::vector<StorageTask> tasks;
            mutex_.lock();

            while (!queue_.empty()) {
                auto task = queue_.front();
                tasks.push_back(task);
                queue_.pop_front();
            }
            mutex_.unlock();

            if (tasks.empty()) {
                continue;
            }

            std::map<uint32_t, std::vector<ServerCompleteTask>> t_tasks;
            for (auto &task : tasks) {
                stat_tasks_ += 1;
                ServerCompleteTask ct = {};
                ct.remote_server_id = task.remote_server_id;
                ct.stoc_req_id = task.stoc_req_id;
                ct.request_type = task.request_type;

                ct.rdma_buf = task.rdma_buf;
                ct.ltc_mr_offset = task.ltc_mr_offset;
                ct.stoc_block_handle = task.stoc_block_handle;

                if (task.request_type ==
                    leveldb::StoCRequestType::STOC_READ_BLOCKS) {
                    leveldb::Slice result;
                    stoc_file_manager_->ReadDataBlock(task.stoc_block_handle,
                                                      task.stoc_block_handle.offset,
                                                      task.stoc_block_handle.size,
                                                      task.rdma_buf, &result);
                    task.stoc_block_handle.size = result.size();
                    NOVA_ASSERT(result.size() <= task.stoc_block_handle.size);
                    stat_read_bytes_ += task.stoc_block_handle.size;
                } else if (task.request_type ==
                           leveldb::StoCRequestType::STOC_PERSIST) {
                    NOVA_ASSERT(task.persist_pairs.size() == 1);
                    leveldb::FileType type = leveldb::FileType::kCurrentFile;
                    for (auto &pair : task.persist_pairs) {
                        leveldb::StoCPersistentFile *stoc_file = stoc_file_manager_->FindStoCFile(
                                pair.stoc_file_id);
                        uint64_t persisted_bytes = stoc_file->Persist(
                                pair.stoc_file_id);
                        stat_write_bytes_ += persisted_bytes;
                        NOVA_LOG(DEBUG) << fmt::format(
                                    "Persisting stoc file {} for sstable {}",
                                    pair.stoc_file_id, pair.sstable_name);

                        leveldb::BlockHandle h = stoc_file->Handle(
                                pair.sstable_name, task.is_meta_blocks);
                        leveldb::StoCBlockHandle rh = {};
                        rh.server_id = NovaConfig::config->my_server_id;
                        rh.stoc_file_id = pair.stoc_file_id;
                        rh.offset = h.offset();
                        rh.size = h.size();
                        ct.stoc_block_handles.push_back(rh);

                        NOVA_ASSERT(leveldb::ParseFileName(pair.sstable_name,
                                                           &type));
                        if (task.is_meta_blocks ||
                            type == leveldb::FileType::kTableFile) {
                            stoc_file->ForceSeal();
                        }
                    }
                } else if (task.request_type ==
                           leveldb::StoCRequestType::STOC_COMPACTION) {
                    leveldb::TableCache table_cache(
                            task.compaction_request->dbname, options_, 0,
                            nullptr);
                    leveldb::VersionFileMap version_files(&table_cache);
                    leveldb::Compaction *compaction = new leveldb::Compaction(
                            &version_files, &icmp_, &options_,
                            task.compaction_request->source_level,
                            task.compaction_request->target_level);
                    compaction->grandparents_ = task.compaction_request->guides;
                    for (int which = 0; which < 2; which++) {
                        compaction->inputs_[which] = task.compaction_request->inputs[which];
                        for (auto meta : compaction->inputs_[which]) {
                            version_files.fn_files_[meta->number] = meta;
                        }
                    }
                    for (auto meta : compaction->grandparents_) {
                        version_files.fn_files_[meta->number] = meta;
                    }

                    // This will delete the subranges.
                    leveldb::SubRanges srs;
                    srs.subranges = task.compaction_request->subranges;
                    srs.AssertSubrangeBoundary(user_comparator_);
                    compaction->input_version_ = &version_files;

                    leveldb::CompactionState *state = new leveldb::CompactionState(
                            compaction, &srs,
                            task.compaction_request->smallest_snapshot);
                    std::function<uint64_t(void)> fn_generator = []() {
                        uint32_t fn = storage_file_number_seq.fetch_add(1);
                        uint64_t stocid =
                                nova::NovaConfig::config->my_server_id +
                                nova::NovaConfig::config->ltc_servers.size();
                        return (stocid << 32) | fn;
                    };
                    {
                        std::vector<const leveldb::FileMetaData *> files;
                        for (int which = 0; which < 2; which++) {
                            for (int i = 0;
                                 i < compaction->num_input_files(which); i++) {
                                files.push_back(compaction->input(which, i));
                            }
                        }
                        FetchMetadataFilesInParallel(files,
                                                     task.compaction_request->dbname,
                                                     options_,
                                                     reinterpret_cast<leveldb::StoCBlockClient *>(client_),
                                                     env_);
                    }
                    leveldb::CompactionJob job(fn_generator, env_,
                                               task.compaction_request->dbname,
                                               user_comparator_,
                                               options_, this, &table_cache);
                    NOVA_LOG(rdmaio::DEBUG)
                        << fmt::format("storage[{}]: {}", thread_id_,
                                       compaction->DebugString(
                                               user_comparator_));
                    auto it = compaction->MakeInputIterator(&table_cache, this);
                    leveldb::CompactionStats stats = state->BuildStats();
                    job.CompactTables(state, it, &stats, true,
                                      leveldb::CompactInputType::kCompactInputSSTables,
                                      leveldb::CompactOutputType::kCompactOutputSSTables);
                    ct.compaction_state = state;
                    ct.compaction_request = task.compaction_request;
                } else {
                    NOVA_ASSERT(false);
                }
                NOVA_LOG(DEBUG)
                    << fmt::format(
                            "CCWorker: Working on t:{} ss:{} req:{} type:{}",
                            task.rdma_server_thread_id, ct.remote_server_id,
                            ct.stoc_req_id,
                            ct.request_type);
                t_tasks[task.rdma_server_thread_id].push_back(ct);
            }

            for (auto &it : t_tasks) {
                rdma_servers_[it.first]->AddCompleteTasks(it.second);
            }
        }
    }
}