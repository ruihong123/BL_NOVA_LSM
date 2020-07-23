
//
// Created by Haoyu Huang on 6/18/20.
// Copyright (c) 2020 University of Southern California. All rights reserved.
//

#include "destination_migration.h"

#include "common/nova_config.h"
#include "log/log_recovery.h"

#include "ltc/compaction_thread.h"
#include "db_helper.h"

namespace leveldb {
    DestinationMigration::DestinationMigration(
            leveldb::StocPersistentFileManager *stoc_file_manager,
            const std::vector<leveldb::EnvBGThread *> &bg_compaction_threads,
            const std::vector<leveldb::EnvBGThread *> &bg_flush_memtable_threads)
            : stoc_file_manager_(stoc_file_manager),
              bg_compaction_threads_(bg_compaction_threads),
              bg_flush_memtable_threads_(bg_flush_memtable_threads) {
    }


    void DestinationMigration::AddReceivedDBId(char *buf,
                                               uint32_t msg_size) {
        mu.lock();
        DBMeta meta = {};
        meta.buf = buf;
        meta.msg_size = msg_size;
        db_metas.push_back(meta);
        mu.unlock();
    }

    void DestinationMigration::Start() {
        while (true) {
            sem_wait(&sem_);

            mu.lock();
            std::vector<DBMeta> rdbs = db_metas;
            db_metas.clear();
            mu.unlock();
            uint32_t cfg_id = nova::NovaConfig::config->current_cfg_id;

            NOVA_ASSERT(cfg_id == 1);

            for (auto dbmeta : rdbs) {
                RecoverDBMeta(dbmeta, cfg_id);
            }
        }

    }
    void
    DestinationMigration::RecoverDBMeta(DBMeta dbmeta, int cfg_id) {
        // Open this new database;
        // Wait for lsm tree metadata.
        // build lsm tree.
        // now accept request.
        NOVA_ASSERT(dbmeta.buf);
        NOVA_ASSERT(dbmeta.buf[0] == StoCRequestType::LTC_MIGRATION);
        char *charbuf = dbmeta.buf;
        charbuf += 1;
        Slice buf(charbuf, nova::NovaConfig::config->max_stoc_file_size);

        uint32_t dbindex;
        uint32_t version_size;
        uint32_t srs_size;
        uint32_t memtable_size;
        uint32_t lookup_index_size;
        uint32_t tableid_mapping_size;
        uint64_t last_sequence = 0;
        uint64_t next_file_number = 0;
        NOVA_ASSERT(DecodeFixed32(&buf, &dbindex));
        NOVA_ASSERT(DecodeFixed32(&buf, &version_size));
        NOVA_ASSERT(DecodeFixed32(&buf, &srs_size));
        NOVA_ASSERT(DecodeFixed32(&buf, &memtable_size));
        NOVA_ASSERT(DecodeFixed32(&buf, &lookup_index_size));
        NOVA_ASSERT(DecodeFixed32(&buf, &tableid_mapping_size));
        NOVA_ASSERT(DecodeFixed64(&buf, &last_sequence));
        NOVA_ASSERT(DecodeFixed64(&buf, &next_file_number));

        auto reorg = new leveldb::LTCCompactionThread(mem_manager_);
        auto coord = new leveldb::LTCCompactionThread(mem_manager_);
        auto client = new leveldb::StoCBlockClient(dbindex,
                                                   stoc_file_manager_);
        auto dbint = CreateDatabase(cfg_id, dbindex, nullptr, nullptr,
                                 mem_manager_, client,
                                 bg_compaction_threads_,
                                 bg_flush_memtable_threads_, reorg,
                                 coord);
        auto frag = nova::NovaConfig::config->cfgs[cfg_id]->fragments[dbindex];
        frag->db = dbint;
        auto db = reinterpret_cast<leveldb::DBImpl *>(dbint);

        db->versions_->Restore(&buf, last_sequence, next_file_number);

        SubRanges *srs = new SubRanges;
        srs->Decode(&buf);
        db->DecodeMemTablePartitions(&buf);
        db->lookup_index_->Decode(&buf);
        db->versions_->DecodeTableIdMapping(&buf);
        db->subrange_manager_->latest_subranges_.store(srs);

        // Recover memtables from log files.
        // TODO: Log File Name
        // TODO: All memtables are immutable.
        std::vector<MemTableLogFilePair> memtables_to_recover;
        for (int i = 0; i < db->partitioned_active_memtables_.size(); i++) {
            auto partition = db->partitioned_active_memtables_[i];
            MemTableLogFilePair pair = {};
            if (partition->memtable) {
                pair.memtable = partition->memtable;
                pair.logfile = nova::LogFileName(
                        db->server_id_,
                        db->dbid_, partition->memtable->memtableid());
                memtables_to_recover.push_back(pair);
            }
            for (int j = 0; j < partition->closed_log_files.size(); j++) {
                pair.memtable = db->versions_->mid_table_mapping_[partition->closed_log_files[j]]->memtable_;
                // TODO: Use the server id of prior configuration.
                pair.logfile = nova::LogFileName(db->server_id_, db->dbid_,
                                                 partition->closed_log_files[j]);
            }
        }

        LogRecovery recover;
        recover.Recover(memtables_to_recover);

        frag->is_ready_ = true;
        frag->is_ready_signal_.SignalAll();
        uint32_t scid = mem_manager_->slabclassid(0, dbmeta.msg_size);
        mem_manager_->FreeItem(0, dbmeta.buf, scid);
    }
}