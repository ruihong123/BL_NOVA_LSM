
//
// Created by Haoyu Huang on 1/11/20.
// Copyright (c) 2020 University of Southern California. All rights reserved.
//

#ifndef LEVELDB_DC_CLIENT_H
#define LEVELDB_DC_CLIENT_H

#include <cstdint>
#include <vector>
#include <infiniband/verbs.h>
#include "db_types.h"

namespace leveldb {
    struct DCBlockHandle {
        uint64_t offset;
        uint64_t size;
    };

    class LEVELDB_EXPORT DCClient {
    public:
        virtual uint32_t
        InitiateReadBlocks(const std::string &dbname, uint64_t file_number,
                           const FileMetaData &meta,
                           const std::vector<DCBlockHandle> &block_handls,
                           char *result) = 0;

        virtual uint32_t
        InitiateReadBlock(const std::string &dbname, uint64_t file_number,
                          const FileMetaData &meta,
                          const DCBlockHandle &block_handle,
                          char *result) = 0;

        // Read the SSTable and return the total size.
        virtual uint32_t
        InitiateReadSSTable(const std::string &dbname, uint64_t file_number,
                            const FileMetaData &meta, char *result) = 0;

        virtual uint32_t InitiateFlushSSTable(const std::string &dbname,
                                              uint64_t file_number,
                                              const FileMetaData &meta,
                                              char *backing_mem) = 0;

        virtual void OnRecv(ibv_wc_opcode type, uint64_t wr_id,
                            int remote_server_id, char *buf,
                            uint32_t imm_data) = 0;

        virtual bool IsDone(uint32_t req_id) = 0;
    };
}


#endif //LEVELDB_DC_CLIENT_H
