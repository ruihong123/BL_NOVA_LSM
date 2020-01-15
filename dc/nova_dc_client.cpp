
//
// Created by Haoyu Huang on 1/8/20.
// Copyright (c) 2020 University of Southern California. All rights reserved.
//

#include "nova_dc_client.h"
#include "nova/nova_config.h"
#include "nova/nova_common.h"

namespace leveldb {

    void NovaDCClient::IncrementReqId() {
        current_req_id_++;
        if (current_req_id_ == 0) {
            current_req_id_ = 1;
        }
    }

    uint32_t NovaDCClient::InitiateFlushSSTable(const std::string &dbname,
                                                uint64_t file_number,
                                                const leveldb::FileMetaData &meta,
                                                char *backing_mem) {
        uint32_t dc_id = HomeDCNode(meta);
        char *sendbuf = rdma_store_->GetSendBuf(dc_id);
        char *buf = sendbuf;
        buf[0] = DCRequestType::DC_FLUSH_SSTABLE;
        buf++;
        buf += leveldb::EncodeStr(buf, dbname);
        leveldb::EncodeFixed32(buf, file_number);
        buf += 4;
        leveldb::EncodeFixed32(buf, meta.file_size);
        rdma_store_->PostSend(sendbuf, 1 + 4 + dbname.size() + 4 + 4, dc_id,
                              current_req_id_);
        DCRequestContext context = {};
        uint32_t req_id = current_req_id_;
        context.done = false;
        context.meta = meta;
        context.file_number = file_number;
        context.sstable_backing_mem = backing_mem;
        context.dbname = dbname;
        request_context_[current_req_id_] = context;
        IncrementReqId();
        rdma_store_->FlushPendingSends(dc_id);
        return req_id;
    }

    uint32_t NovaDCClient::HomeDCNode(const leveldb::FileMetaData &meta) {
        uint64_t key = nova::keyhash(meta.smallest.user_key().data(),
                                     meta.smallest.user_key().size());
        nova::Fragment *frag = nova::NovaDCConfig::home_fragment(key);
        RDMA_ASSERT(frag != nullptr);
        return frag->server_ids[0];
    }

    uint32_t NovaDCClient::InitiateReadSSTable(const std::string &dbname,
                                               uint64_t file_number,
                                               const leveldb::FileMetaData &meta,
                                               char *result) {
        // The request contains dbname, file number, and remote offset to accept the read SSTable.
        // DC issues a WRITE_IMM to write the read SSTable into the remote offset providing the request id.
        uint32_t dc_id = HomeDCNode(meta);
        char *sendbuf = rdma_store_->GetSendBuf(dc_id);
        sendbuf[0] = DCRequestType::DC_READ_SSTABLE;
        uint32_t msg_size = 1;
        msg_size += leveldb::EncodeStr(sendbuf + msg_size, dbname);
        leveldb::EncodeFixed32(sendbuf + msg_size, file_number);
        msg_size += 4;
        leveldb::EncodeFixed64(sendbuf + msg_size, (uint64_t) result);
        msg_size += 8;
        rdma_store_->PostSend(sendbuf, msg_size, dc_id,
                              current_req_id_);

        DCRequestContext context = {};
        uint32_t req_id = current_req_id_;
        context.done = false;
        request_context_[current_req_id_] = context;
        IncrementReqId();
        rdma_store_->FlushPendingSends(dc_id);
        return req_id;
    }

    uint32_t NovaDCClient::InitiateReadBlock(const std::string &dbname,
                                             uint64_t file_number,
                                             const leveldb::FileMetaData &meta,
                                             const leveldb::DCBlockHandle &block_handle,
                                             char *result) {
        std::vector<leveldb::DCBlockHandle> handles;
        handles.emplace_back(block_handle);
        return InitiateReadBlocks(dbname, file_number, meta, handles, result);
    }

    uint32_t NovaDCClient::InitiateDeleteFiles(const std::string &dbname,
                                               const std::vector<FileMetaData> &filenames) {
        // Does not need response.
        std::map<uint32_t, std::vector<uint64_t>> dc_files;
        for (const auto &file : filenames) {
            uint32_t dc_id = HomeDCNode(file);
            dc_files[dc_id].push_back(file.number);
        }

        for (auto it = dc_files.begin(); it != dc_files.end(); it++) {
            char *sendbuf = rdma_store_->GetSendBuf(it->first);
            sendbuf[0] = DCRequestType::DC_DELETE_TABLES;
            uint32_t msg_size = 1;
            msg_size += leveldb::EncodeStr(sendbuf + msg_size, dbname);
            leveldb::EncodeFixed32(sendbuf + msg_size, it->second.size());
            msg_size += 4;
            for (int i = 0; i < it->second.size(); i++) {
                leveldb::EncodeFixed64(sendbuf + msg_size, it->second[i]);
                msg_size += 8;
            }
            rdma_store_->PostSend(sendbuf, msg_size, it->first,
                                  current_req_id_);
        }
        IncrementReqId();
        return 0;
    }

    uint32_t NovaDCClient::InitiateReplicateLogRecords(
            const std::string &log_file_name, const leveldb::Slice &slice) {
        // Synchronous replication.
        rdma_log_writer_->AddRecord(log_file_name, slice);
        return 0;
    }

    uint32_t
    NovaDCClient::InitiateCloseLogFile(const std::string &log_file_name) {
        rdma_log_writer_->CloseLogFile(log_file_name);
        return 0;
    }

    uint32_t NovaDCClient::InitiateReadBlocks(const std::string &dbname,
                                              uint64_t file_number,
                                              const leveldb::FileMetaData &meta,
                                              const std::vector<leveldb::DCBlockHandle> &block_handls,
                                              char *result) {
        // The request contains dbname, file number, block handles, and remote offset to accept the read blocks.
        // DC issues a WRITE_IMM to write the read blocks into the remote offset providing the request id.
        uint32_t dc_id = HomeDCNode(meta);
        char *sendbuf = rdma_store_->GetSendBuf(dc_id);
        sendbuf[0] = DCRequestType::DC_READ_BLOCKS;
        uint32_t msg_size = 1;
        msg_size += leveldb::EncodeStr(sendbuf + msg_size, dbname);
        leveldb::EncodeFixed32(sendbuf + msg_size, file_number);
        msg_size += 4;
        leveldb::EncodeFixed32(sendbuf + msg_size, block_handls.size());
        msg_size += 4;
        for (const auto &handle : block_handls) {
            leveldb::EncodeFixed64(sendbuf + msg_size, handle.offset);
            msg_size += 8;
            leveldb::EncodeFixed64(sendbuf + msg_size, handle.size);
            msg_size += 8;
        }
        leveldb::EncodeFixed64(sendbuf + msg_size, (uint64_t) result);
        msg_size += 8;
        rdma_store_->PostSend(sendbuf, msg_size, dc_id,
                              current_req_id_);
        uint32_t req_id = current_req_id_;
        DCRequestContext context = {};
        context.done = false;
        request_context_[current_req_id_] = context;
        IncrementReqId();
        rdma_store_->FlushPendingSends(dc_id);
        return req_id;
    }

    bool NovaDCClient::IsDone(uint32_t req_id) {
        // Poll both queues.
        rdma_store_->PollRQ();
        rdma_store_->PollSQ();
        auto context_it = request_context_.find(req_id);
        RDMA_ASSERT(context_it != request_context_.end());
        if (context_it->second.done) {
            request_context_.erase(req_id);
            return true;
        }
        return false;
    }

    void
    NovaDCClient::OnRecv(ibv_wc_opcode type, uint64_t wr_id,
                         int remote_server_id,
                         char *buf,
                         uint32_t imm_data) {
        uint32_t req_id = imm_data;
        switch (type) {
            case IBV_WC_SEND:
                break;
            case IBV_WC_RDMA_WRITE:
                rdma_log_writer_->AckWriteSuccess(remote_server_id, wr_id);
                break;
            case IBV_WC_RDMA_READ:
                break;
            case IBV_WC_RECV:
            case IBV_WC_RECV_RDMA_WITH_IMM:
                if (buf[0] ==
                    DCRequestType::DC_ALLOCATE_LOG_BUFFER_SUCC) {
                    uint64_t base = leveldb::DecodeFixed64(buf + 1);
                    uint64_t size = leveldb::DecodeFixed64(buf + 9);
                    rdma_log_writer_->AckAllocLogBuf(remote_server_id, base,
                                                     size);
                } else if (buf[0] == DCRequestType::DC_FLUSH_SSTABLE_BUF) {
                    uint64_t remote_dc_offset = leveldb::DecodeFixed64(buf + 1);
                    auto context_it = request_context_.find(req_id);
                    RDMA_ASSERT(context_it != request_context_.end());
                    const DCRequestContext &context = context_it->second;
                    rdma_store_->PostWrite(
                            context.sstable_backing_mem,
                            context.meta.file_size,
                            remote_server_id, remote_dc_offset,
                            false, req_id);
                    rdma_store_->FlushPendingSends(remote_server_id);
                } else if (buf[0] == DCRequestType::DC_FLUSH_SSTABLE_SUCC) {
                    auto context_it = request_context_.find(req_id);
                    RDMA_ASSERT(context_it != request_context_.end());
                    context_it->second.done = true;
                } else if (buf[0] == DCRequestType::DC_READ_BLOCKS) {
                    auto context_it = request_context_.find(req_id);
                    RDMA_ASSERT(context_it != request_context_.end());
                    context_it->second.done = true;
                } else if (buf[0] == DCRequestType::DC_READ_SSTABLE) {
                    auto context_it = request_context_.find(req_id);
                    RDMA_ASSERT(context_it != request_context_.end());
                    context_it->second.done = true;
                } else {
                    RDMA_ASSERT(false) << "memstore[" << rdma_store_->store_id()
                                       << "]: unknown recv from "
                                       << remote_server_id << " buf:"
                                       << buf;
                }
                break;
        }
    }

}