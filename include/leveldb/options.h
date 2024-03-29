// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_INCLUDE_OPTIONS_H_
#define STORAGE_LEVELDB_INCLUDE_OPTIONS_H_

#include <stddef.h>
#include <string>

#include "leveldb/export.h"
#include "leveldb/stoc_client.h"
#include "log_writer.h"
#include "env.h"
#include "env_bg_thread.h"

namespace leveldb {

    class Cache;

    class Comparator;

    class Env;

    class FilterPolicy;

    class Logger;

    class Snapshot;

// DB contents are stored in a set of blocks, each of which holds a
// sequence of key,value pairs.  Each block may be compressed before
// being stored in a file.  The following enum describes which
// compression method (if any) is used to compress a block.
    enum CompressionType {
        // NOTE: do not change the values of existing entries, as these are
        // part of the persistent format on disk.
        kNoCompression = 0x0,
        kSnappyCompression = 0x1
    };

    enum MemTableType {
        kMemTablePool = 0,
        kStaticPartition = 1,
    };

    enum MajorCompactionType {
        kMajorDisabled = 0,
        kMajorSingleThreaded = 1,
        kMajorCoordinated = 2,
        kMajorCoordinatedStoC = 3
    };

    enum ClientAccessPattern {
        kClientAccessSkewed = 0,
        kClientAccessUniform = 1,
    };

    // Options to control the behavior of a database (passed to DB::Open)
    struct LEVELDB_EXPORT Options {
        // Create an Options object with default values for all fields.
        Options();

        // -------------------
        // Parameters that affect behavior

        // Comparator used to define the order of keys in the table.
        // Default: a comparator that uses lexicographic byte-wise ordering
        //
        // REQUIRES: The client must ensure that the comparator supplied
        // here has the same name and orders keys *exactly* the same as the
        // comparator provided to previous open calls on the same DB.
        const Comparator *comparator = nullptr;

        bool debug = false;

        MemManager *mem_manager = nullptr;
        uint32_t num_recovery_thread = 0;

        StoCClient *stoc_client = nullptr;

        std::vector<uint32_t> manifest_stoc_ids;

        uint32_t num_tiny_ranges_per_subrange = 10;

        double subrange_reorg_sampling_ratio = 1.0;

        uint32_t max_num_coordinated_compaction_nonoverlapping_sets = 1;

        uint32_t max_num_sstables_in_nonoverlapping_set = 20;

        std::string zipfian_dist_file_path = "/tmp/zipfian";

        // Enable tracing accesses.
        bool enable_tracing = false;

        ClientAccessPattern client_access_pattern;

        // Trace file path to log accesses.
        std::string trace_file_path = "/tmp/leveldb_trace_log";

        // If true, the database will be created if it is missing.
        bool create_if_missing = false;

        // If true, an error is raised if the database already exists.
        bool error_if_exists = false;

        MajorCompactionType major_compaction_type = MajorCompactionType::kMajorSingleThreaded;

        bool enable_flush_multiple_memtables = false;

        bool enable_subrange_reorg = false;

        // If true, the implementation will do aggressive checking of the
        // data it is processing and will stop early if it detects any
        // errors.  This may have unforeseen ramifications: for example, a
        // corruption of one DB entry may cause a large number of entries to
        // become unreadable or for the entire DB to become unopenable.
        bool paranoid_checks = false;

        bool prune_memtable_before_flushing = false;

        // Use the specified object to interact with the environment,
        // e.g. to read/write files, schedule background work, etc.
        // Default: Env::Default()
        Env *env = nullptr;

        std::vector<EnvBGThread *> bg_compaction_threads = {};
        std::vector<EnvBGThread *> bg_flush_memtable_threads = {};
        EnvBGThread *reorg_thread = nullptr;
        EnvBGThread *compaction_coordinator_thread = nullptr;
        //total number of memtables
        uint32_t num_memtables = 2;

        MemTableType memtable_type = MemTableType::kStaticPartition;

        bool enable_subranges = false;
        bool enable_detailed_stats = true;

        // 4 GB.
        uint64_t l0bytes_start_compaction_trigger = 4l * 1024 * 1024 * 1024;
        uint64_t l0bytes_stop_writes_trigger = 0;
        uint64_t l0nfiles_start_compaction_trigger = 4;
        int level = 0;

        uint32_t num_memtable_partitions = 1;

        bool enable_lookup_index = false;
        bool enable_range_index = false;

        uint32_t subrange_no_flush_num_keys = 100;
        uint32_t num_compaction_threads = 0;

        uint64_t lower_key = 0;
        uint64_t upper_key = 0;

        // Any internal progress/error information generated by the db will
        // be written to info_log if it is non-null, or to a file stored
        // in the same directory as the DB contents if info_log is null.
        Logger *info_log = nullptr;

        // -------------------
        // Parameters that affect performance

        // Amount of data to build up in memory (backed by an unsorted log
        // on disk) before converting to a sorted on-disk file.
        //
        // Larger values increase performance, especially during bulk loads.
        // Up to two write buffers may be held in memory at the same time,
        // so you may wish to adjust this parameter to control memory usage.
        // Also, a larger write buffer will result in a longer recovery time
        // the next time the database is opened.
        size_t write_buffer_size = 2 * 1024 * 1024;

        // Number of open files that can be used by the DB.  You may need to
        // increase this if your database has a large working set (budget
        // one open file per 2MB of working set).
        int max_open_files = 1000;

        // Control over blocks (user data is stored in a set of blocks, and
        // a block is the unit of reading from disk).

        // If non-null, use the specified cache for blocks.
        // If null, leveldb will automatically create and use an 8MB internal cache.
        Cache *block_cache = nullptr;

        // Approximate size of user data packed per block.  Note that the
        // block size specified here corresponds to uncompressed data.  The
        // actual size of the unit read from disk may be smaller if
        // compression is enabled.  This parameter can be changed dynamically.
        size_t block_size = 8 * 1024;

        // Number of keys between restart points for delta encoding of keys.
        // This parameter can be changed dynamically.  Most clients should
        // leave this parameter alone.
        int block_restart_interval = 16;

        // Leveldb will write up to this amount of bytes to a file before
        // switching to a new one.
        // Most clients should leave this parameter alone.  However if your
        // filesystem is more efficient with larger files, you could
        // consider increasing the value.  The downside will be longer
        // compactions and hence longer latency/performance hiccups.
        // Another reason to increase this parameter might be when you are
        // initially populating a large database.
        size_t max_file_size = 64 * 1024 * 1024;

        // The maximum log file size a MC maintains.
        // When the log file is full, MC flushes the log to DC.
        size_t max_log_file_size = 4 * 1024 * 1024;

        size_t max_stoc_file_size = 4 * 1024 * 1024 + 1024 * 1024;

        // Compress blocks using the specified compression algorithm.  This
        // parameter can be changed dynamically.
        //
        // Default: kSnappyCompression, which gives lightweight but fast
        // compression.
        //
        // Typical speeds of kSnappyCompression on an Intel(R) Core(TM)2 2.4GHz:
        //    ~200-500MB/s compression
        //    ~400-800MB/s decompression
        // Note that these speeds are significantly faster than most
        // persistent storage speeds, and therefore it is typically never
        // worth switching to kNoCompression.  Even if the input data is
        // incompressible, the kSnappyCompression implementation will
        // efficiently detect that and will switch to uncompressed mode.
        CompressionType compression = kSnappyCompression;

        // If non-null, use the specified filter policy to reduce disk reads.
        // Many applications will benefit from passing the result of
        // NewBloomFilterPolicy() here.
        const FilterPolicy *filter_policy = nullptr;

        MemTablePool *memtable_pool = nullptr;
    };

// Options that control read operations
    struct LEVELDB_EXPORT ReadOptions {
        ReadOptions() = default;

        // If true, all data read from underlying storage will be
        // verified against corresponding checksums.
        bool verify_checksums = false;

        // Should the data read for this iteration be cached in memory?
        // Callers may wish to set this field to false for bulk scans.
        bool fill_cache = true;

        uint64_t thread_id;
        uint32_t cfg_id;

        MemManager *mem_manager = nullptr;

        StoCClient *stoc_client = nullptr;
        char *rdma_backing_mem = nullptr;
        uint32_t rdma_backing_mem_size = 0;

        uint64_t hash = 0;

        // If "snapshot" is non-null, read as of the supplied snapshot
        // (which must belong to the DB that is being read and which must
        // not have been released).  If "snapshot" is null, use an implicit
        // snapshot of the state at the beginning of this read operation.
        const Snapshot *snapshot = nullptr;
    };

// Options that control write operations
    struct LEVELDB_EXPORT WriteOptions {
        WriteOptions() = default;

        bool local_write = false;
        unsigned int *rand_seed;
        bool is_loading_db = false;

        uint64_t hash = 0;
        // For replicating log records.
        uint64_t thread_id;
        StoCClient *stoc_client = nullptr;

        uint64_t total_writes = 0;
        char *rdma_backing_mem = nullptr;
        uint32_t rdma_backing_mem_size = 0;
        StoCReplicateLogRecordState *replicate_log_record_states = nullptr;
    };

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_OPTIONS_H_
