
//
// Created by Haoyu Huang on 1/11/20.
// Copyright (c) 2020 University of Southern California. All rights reserved.
//

#ifndef LEVELDB_STOC_FILE_CLIENT_IMPL_H
#define LEVELDB_STOC_FILE_CLIENT_IMPL_H

#include <semaphore.h>

#include "util/env_mem.h"
#include "stoc_client_impl.h"
#include "leveldb/env.h"
#include "leveldb/table.h"

namespace leveldb {

    // A stoc writable file client is implemented based on memfile.
    // It first writes data to its memory and then RDMA WRITEs to StoCs.
    class StoCWritableFileClient : public MemFile {
    public:
        StoCWritableFileClient(Env *env,
                               const Options &options,
                               uint64_t file_number,
                               MemManager *mem_manager,
                               StoCClient *stoc_client,
                               const std::string &dbname,
                               uint64_t thread_id,
                               uint64_t file_size, unsigned int *rand_seed,
                               std::string &filename);

        ~StoCWritableFileClient();

        uint64_t Size() const override { return used_size_; }

        Status
        Read(uint64_t offset, size_t n, Slice *result, char *scratch) override;

        Status Write(uint64_t offset, const Slice &data) override;

        Status Append(const Slice &data) override;

        char *Buf();

        Status Append(uint32_t size);

        Status Fsync() override;

        Status SyncAppend(const Slice &data, uint32_t stoc_id);

        void Format();

        void WaitForPersistingDataBlocks();

        uint32_t Finalize();

        const char *backing_mem() override { return backing_mem_; }

        void set_meta(const FileMetaData &meta) { meta_ = meta; }

        uint64_t used_size() { return used_size_; }

        uint64_t allocated_size() { return allocated_size_; }

        uint64_t thread_id() { return thread_id_; }

        uint64_t file_number() { return file_number_; }

        void set_num_data_blocks(uint32_t num_data_blocks) {
            num_data_blocks_ = num_data_blocks;
        }

        StoCBlockHandle meta_block_handle() {
            return meta_block_handle_;
        }

        std::vector<StoCBlockHandle> rhs() {
            std::vector<StoCBlockHandle> rhs;
            for (int i = 0; i < status_.size(); i++) {
                rhs.push_back(status_[i].result_handle);
            }
            return rhs;
        }

    private:
        struct PersistStatus {
            uint32_t remote_server_id = 0;
            uint32_t WRITE_req_id = 0;
            StoCBlockHandle result_handle;
        };

        uint32_t WriteBlock(BlockBuilder *block, uint64_t offset);

        uint32_t WriteRawBlock(const Slice &block_contents,
                               CompressionType type,
                               uint64_t offset);

        Env *mem_env_ = nullptr;
        unsigned int *rand_seed_ = nullptr;
        uint64_t file_number_ = 0;
        const std::string fname_;
        MemManager *mem_manager_ = nullptr;
        StoCClient *stoc_client_ = nullptr;
        const std::string &dbname_;
        FileMetaData meta_;
        uint64_t thread_id_ = 0;

        Block *index_block_ = nullptr;
        int num_data_blocks_ = 0;
        const Options &options_;

        char *backing_mem_ = nullptr;
        uint64_t allocated_size_ = 0;
        uint64_t used_size_ = 0;
        std::vector<int> nblocks_in_group_;
        std::vector<PersistStatus> status_;
        StoCBlockHandle meta_block_handle_;
    };

    class StoCRandomAccessFileClientImpl : public StoCRandomAccessFileClient {
    public:
        StoCRandomAccessFileClientImpl(Env *env, const Options &options,
                                       const std::string &dbname,
                                       uint64_t file_number,
                                       const FileMetaData *meta,
                                       StoCClient *stoc_client,
                                       MemManager *mem_manager,
                                       uint64_t thread_id,
                                       bool prefetch_all,
                                       std::string &filename);

        ~StoCRandomAccessFileClientImpl() override;

        Status
        Read(const StoCBlockHandle &stoc_block_handle, uint64_t offset,
             size_t n,
             Slice *result, char *scratch) override;

        Status
        Read(const ReadOptions &read_options,
             const StoCBlockHandle &block_handle,
             uint64_t offset, size_t n,
             Slice *result, char *scratch) override;

        Status ReadAll(StoCClient *stoc_client);

    private:
        struct DataBlockStoCFileLocalBuf {
            uint64_t offset;
            uint32_t size;
            uint64_t local_offset;
        };

        const std::string &dbname_;
        uint64_t file_number_;
        const FileMetaData *meta_;
        const std::string filename;

        bool prefetch_all_ = false;
        char *backing_mem_table_ = nullptr;

        std::unordered_map<uint64_t, DataBlockStoCFileLocalBuf> stoc_local_offset_;
        std::mutex mutex_;

        MemManager *mem_manager_ = nullptr;
        uint64_t thread_id_ = 0;
        uint32_t dbid_ = 0;
        Env *env_ = nullptr;
        RandomAccessFile *local_ra_file_ = nullptr;
    };

    struct DeleteTableRequest {
        std::string dbname;
        uint32_t file_number;
    };
}


#endif //LEVELDB_STOC_FILE_CLIENT_IMPL_H
