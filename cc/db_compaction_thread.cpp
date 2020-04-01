
//
// Created by Haoyu Huang on 2/27/20.
// Copyright (c) 2020 University of Southern California. All rights reserved.
//

#include "db_compaction_thread.h"

namespace leveldb {

    NovaCCCompactionThread::NovaCCCompactionThread(MemManager *mem_manager)
            : mem_manager_(mem_manager) {
        sem_init(&signal, 0, 0);
    }

    bool NovaCCCompactionThread::Schedule(const CompactionTask &task) {
        background_work_mutex_.Lock();
        background_work_queue_.push_back(task);
        background_work_mutex_.Unlock();
        sem_post(&signal);
        return true;
    }

    bool NovaCCCompactionThread::IsInitialized() {
        background_work_mutex_.Lock();
        bool is_running = is_running_;
        background_work_mutex_.Unlock();
        return is_running;
    }

    uint32_t NovaCCCompactionThread::num_running_tasks() {
        return num_tasks_;
    }

    void NovaCCCompactionThread::Start() {
        nova::NovaConfig::config->add_tid_mapping();

        background_work_mutex_.Lock();
        is_running_ = true;
        background_work_mutex_.Unlock();

        rand_seed_ = thread_id_ + 100000;

        RDMA_LOG(rdmaio::INFO) << "Compaction workers started";
        while (is_running_) {
            sem_wait(&signal);

            background_work_mutex_.Lock();
            if (background_work_queue_.empty()) {
                background_work_mutex_.Unlock();
                continue;
            }

            std::vector<CompactionTask> tasks(background_work_queue_);
            background_work_queue_.clear();
            background_work_mutex_.Unlock();

            num_tasks_ += tasks.size();

//            auto db = reinterpret_cast<DB *>(tasks[0].db);
//            db->PerformCompaction(this, tasks);

            std::map<void *, std::vector<CompactionTask>> db_tasks;
            for (auto &task : tasks) {
                db_tasks[task.db].push_back(task);
            }

            for (auto& it : db_tasks) {
                auto db = reinterpret_cast<DB *>(it.first);
                db->PerformCompaction(this, it.second);
            }
        }
    }
}