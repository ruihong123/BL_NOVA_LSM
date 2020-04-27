
//
// Created by Haoyu Huang on 2/27/20.
// Copyright (c) 2020 University of Southern California. All rights reserved.
//

#ifndef LEVELDB_DB_COMPACTION_THREAD_H
#define LEVELDB_DB_COMPACTION_THREAD_H

#include "leveldb/db_types.h"
#include <semaphore.h>
#include "port/port.h"
#include "nova_cc_client.h"
#include "leveldb/env_bg_thread.h"

namespace leveldb {

    class NovaNoopCompactionThread : public EnvBGThread {
    public:
        NovaNoopCompactionThread() {
            for (int i = 0 ; i < BUCKET_SIZE; i++) {
                memtable_size[i] = 0;
            }
        }

        bool Schedule(const EnvBGTask &task) {
            std::vector<EnvBGTask> tasks;
            tasks.push_back(task);
            db->TestCompact(this, tasks);
            return true;
        };

        uint64_t thread_id() override { return thread_id_; }

        uint32_t num_running_tasks() {
            return 0;
        };

        CCClient *dc_client() override {
            return cc_client_;
        };

        MemManager *mem_manager() override {
            return mem_manager_;
        };

        unsigned int *rand_seed() override {
            return &rand_seed_;
        }


        bool IsInitialized() {
            return true;
        };

        void Start() {

        }

        DB *db = nullptr;
        uint64_t thread_id_ = 0;

        NovaBlockCCClient *cc_client_ = nullptr;

    private:
        port::Mutex background_work_mutex_;
        sem_t signal;
        std::vector<EnvBGTask> background_work_queue_
        GUARDED_BY(background_work_mutex_);
        std::atomic_int_fast32_t num_tasks_;

        MemManager *mem_manager_ = nullptr;
        bool is_running_ = false;
        unsigned int rand_seed_;
    };

    class NovaCCCompactionThread : public EnvBGThread {
    public:
        explicit NovaCCCompactionThread(MemManager *mem_manager);

        bool Schedule(const EnvBGTask &task) override;

        uint64_t thread_id() override { return thread_id_; }

        uint32_t num_running_tasks() override;

        CCClient *dc_client() override {
            return cc_client_;
        };

        MemManager *mem_manager() override {
            return mem_manager_;
        };

        unsigned int *rand_seed() override {
            return &rand_seed_;
        }


        bool IsInitialized() override;

        void Start();

        uint64_t thread_id_ = 0;

        NovaBlockCCClient *cc_client_ = nullptr;

    private:
        port::Mutex background_work_mutex_;
        sem_t signal;
        std::vector<EnvBGTask> background_work_queue_
        GUARDED_BY(background_work_mutex_);
        std::atomic_int_fast32_t num_tasks_;

        MemManager *mem_manager_ = nullptr;
        bool is_running_ = false;
        unsigned int rand_seed_;
    };
}

#endif //LEVELDB_DB_COMPACTION_THREAD_H
