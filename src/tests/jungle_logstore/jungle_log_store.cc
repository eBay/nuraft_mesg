#include "jungle_log_store.h"

#include "cast_helper.h"
#include "common.h"
#include "stat.h"
#include "storage_engine_buffer.h"

#include "lz4.h"

#include <map>
#include <mutex>

#include <assert.h>

using namespace nukv;

namespace nuraft {

ptr<buffer> zero_buf;

struct jungle_log_store::log_cache {
    log_cache(const jungle_log_store::Options& opt = jungle_log_store::Options())
        : maxCacheSize(opt.maxCacheSizeBytes)
        , maxLogsToKeep(opt.maxCachedLogs)
        , logsSize(0)
        , curStartIdx(0)
        {}

    void set(ulong log_idx, ptr<log_entry> log) {
        static StatElem& cache_min_idx =    *StatMgr::getInstance()->createStat
               ( StatElem::GAUGE,           "raft_log_cache_min_index" );
        static StatElem& cache_max_idx =    *StatMgr::getInstance()->createStat
               ( StatElem::GAUGE,           "raft_log_cache_max_index" );
        static StatElem& cache_usage =      *StatMgr::getInstance()->createStat
               ( StatElem::GAUGE,           "raft_log_cache_usage" );

        std::unique_lock<std::mutex> l(lock);

        // Check if already exists.
        {   auto entry = logs.find(log_idx);
            if (entry != logs.end()) {
                // Same log index already exists,
                // we will overwrite it. Substract the existing size first.
                ptr<log_entry>& log_to_erase = entry->second;
                logsSize.fetch_sub( sizeof(uint64_t)*2 +
                                    log_to_erase->get_buf().container_size() );
                logs.erase(entry);
            }
        }

        // Insert into map.
        logs[log_idx] = log;
        logsSize.fetch_add( sizeof(uint64_t)*2 + log->get_buf().container_size() );

        // Check min/max index numbers.
        auto min = logs.begin();
        uint64_t min_log_idx = (min != logs.end()) ? min->first : 0;
        auto max = logs.rbegin();
        uint64_t max_log_idx = (max != logs.rend()) ? max->first : 0;

        // If the number of logs in the map exceeds the limit,
        // purge logs in the front.
        if ( max_log_idx > maxLogsToKeep + min_log_idx ||
             logsSize > maxCacheSize ) {
            size_t count = 0;
            auto itr = logs.begin();
            while (itr != logs.end()) {
                ptr<log_entry>& log_to_erase = itr->second;
                logsSize.fetch_sub( sizeof(uint64_t)*2 +
                                    log_to_erase->get_buf().container_size() );
                itr = logs.erase(itr);
                count++;
                if ( count + min_log_idx + maxLogsToKeep >= max_log_idx &&
                     logsSize <= maxCacheSize ) break;
            }
        }
        min = logs.begin();
        min_log_idx = (min != logs.end()) ? min->first : 0;

        // Update stats.
        cache_min_idx = min_log_idx;
        cache_max_idx = max_log_idx;
        cache_usage = logsSize;
    }

    // If cache is empty, return 0.
    uint64_t nextSlot() {
        std::unique_lock<std::mutex> l(lock);
        if (!logs.size()) return 0;
        auto max = logs.rbegin();
        uint64_t max_log_idx = (max != logs.rend()) ? max->first : 0;
        if (max_log_idx) return max_log_idx + 1;
        return 0;
    }

    void compact(ulong log_idx_upto) {
        static StatElem& cache_usage =      *StatMgr::getInstance()->createStat
               ( StatElem::GAUGE,           "raft_log_cache_usage" );

        std::unique_lock<std::mutex> l(lock);
        auto itr = logs.begin();
        while (itr != logs.end()) {
            // Purge logs in the front.
            // `log_idx_upto` itself also will be purged,
            // and next min index will be `log_idx_upto + 1`.
            if (itr->first > log_idx_upto) break;

            ptr<log_entry>& log_to_erase = itr->second;
            logsSize.fetch_sub( sizeof(uint64_t)*2 +
                                log_to_erase->get_buf().container_size() );
            itr = logs.erase(itr);
        }
        cache_usage = logsSize;
    }

    ptr<log_entry> get(ulong log_idx) {
        // Point query.
        std::unique_lock<std::mutex> l(lock);
        auto itr = logs.find(log_idx);
        if (itr == logs.end()) return nullptr;
        return itr->second;
    }

    bool gets( ulong start,
               ulong end,
               int64_t size_hint,
               ptr< std::vector< ptr<log_entry> > >& vector_out )
    {
        int64_t cur_size = 0;

        // Range query.
        std::unique_lock<std::mutex> l(lock);

        // Check min index number. If request's start number is
        // earlier than this cache, just return false.
        auto min = logs.begin();
        uint64_t min_log_idx = (min != logs.end()) ? min->first : 0;
        if (!min_log_idx || min_log_idx > start) return false;

        size_t cur_idx = 0;
        auto itr = logs.find(start);
        while ( itr != logs.end() &&
                cur_idx + start < end ) {
            ptr<log_entry>& le = itr->second;
            (*vector_out)[cur_idx] = le;
            cur_idx++;
            itr++;

            if (!le->is_buf_null()) {
                cur_size += le->get_buf().container_size();
            }
            if (size_hint > 0 && cur_size >= size_hint) {
                vector_out->resize(cur_idx);
                break;
            }
        }

        return true;
    }

    void drop() {
        static StatElem& cache_usage =      *StatMgr::getInstance()->createStat
               ( StatElem::GAUGE,           "raft_log_cache_usage" );

        std::unique_lock<std::mutex> l(lock);
        logs.clear();
        logsSize.store(0);
        cache_usage = logsSize;
    }

    uint64_t getStartIndex() const { return curStartIdx.load(); }

    void setStartIndex(uint64_t to) { curStartIdx = to; }

    uint64_t maxCacheSize;

    uint64_t maxLogsToKeep;

    // Cached logs and its lock.
    std::mutex lock;
    std::map< ulong, ptr<log_entry> > logs;

    // Currently cached data size.
    std::atomic<uint64_t> logsSize;

    // Current start index, to avoid access to underlying DB.
    // This value is separate and irrelevant to min value of `logs`.
    std::atomic<uint64_t> curStartIdx;
};

static size_t jl_calc_len(ptr<log_entry>& src) {
    // Term                 8 bytes
    // Type                 1 byte
    // Data length (=N)     4 bytes
    // Data                 N bytes
    return sizeof(uint64_t) + sizeof(uint8_t) + sizeof(uint32_t) +
           src->get_buf().size();
}

static void jl_enc_le(ptr<log_entry>& src, SEBufSerializer& ss) {
    // `log_entry` to binary.
    ss.putU64(src->get_term());
    ss.putU8( _SC(uint8_t, src->get_val_type()) );
    buffer& buf = src->get_buf();
    buf.pos(0);
    ss.putSEBuf( SEBuf(buf.size(), buf.data()) );
}

static ptr<log_entry> jl_dec_le(const SEBuf& src) {
    // binary to `log_entry.
    SEBufSerializer ss(src);
    uint64_t term = ss.getU64();
    log_val_type type = _SC(log_val_type, ss.getU8());
    SEBuf data = ss.getSEBuf();
    ptr<buffer> _data = buffer::alloc(data.len);
    _data->pos(0);
    memcpy(_data->data(), data.buf, data.len);

    return cs_new<log_entry>(term, _data, type);
}

struct jungle_log_store::FlushElem {
    FlushElem(uint64_t desired = 0)
        : desiredLogIdx(desired)
        , done(false)
        {}

    /**
     * Awaiter for caller.
     */
    EventAwaiter eaCaller;

    /**
     * Desired Raft log index to be durable.
     */
    uint64_t desiredLogIdx;

    /**
     * `true` if the request is processed.
     */
    std::atomic<bool> done;
};

ssize_t jungle_log_store::getCompMaxSize(jungle::DB* db,
                                         const jungle::Record& rec)
{
    if (!myOpt.compression) {
        // LZ4 is available, but compression is disabled.
        return 0;
    }
    return LZ4_compressBound(rec.kv.value.size) + 2;
}

ssize_t jungle_log_store::compress(jungle::DB* db,
                                   const jungle::Record& src,
                                   jungle::SizedBuf& dst)
{
    if (!myOpt.compression || dst.size < 2) {
        return 0;
    }

    // NOTE: For future extension, we will make this format
    //       compatible with the one in `storage_engine_jungle`.
    //
    //   << Internal meta format >>
    // Meta length (for future extension)       1 byte
    // Compression type                         1 byte
    dst.data[0] = 1;
    dst.data[1] = 1;

    ssize_t comp_size =
        LZ4_compress_default( (char*)src.kv.value.data,
                              (char*)dst.data + 2,
                              src.kv.value.size,
                              dst.size - 2 );
    return comp_size + 2;
}

ssize_t jungle_log_store::decompress(jungle::DB* db,
                                     const jungle::SizedBuf& src,
                                     jungle::SizedBuf& dst)
{
    return LZ4_decompress_safe( (char*)src.data + 2,
                                (char*)dst.data,
                                src.size - 2,
                                dst.size );
}

void jungle_log_store::flushLoop() {
    // Flush loop is not necessary if strong durability option is disabled.
    if (!myOpt.strongDurability) return;

    while (!flushThreadStopSignal) {
        std::list< std::shared_ptr<FlushElem> > reqs;
        {   std::lock_guard<std::mutex> l(flushReqsLock);
            reqs = flushReqs;
            flushReqs.clear();
        }
        if (!reqs.size()) {
            // No request, sleep.
            eaFlushThread.wait_us(myOpt.flushThreadSleepTimeUs);
            eaFlushThread.reset();
            continue;
        }

        // Find the max desired log index to check if actual
        // flush is needed.
        bool flush_required = false;
        for (auto& entry: reqs) {
            std::shared_ptr<FlushElem> cur = entry;
            if (cur->desiredLogIdx > lastDurableLogIdx) {
                flush_required = true;
                break;
            }
        }

        if (flush_required) {
            flush();
        }

        for (auto& entry: reqs) {
            std::shared_ptr<FlushElem> cur = entry;
            if (cur->desiredLogIdx <= lastDurableLogIdx) {
                cur->done = true;
                cur->eaCaller.invoke();
            } else {
                // Try it again.
                std::lock_guard<std::mutex> l(flushReqsLock);
                flushReqs.push_back(cur);
            }
        }
    }
}

jungle_log_store::jungle_log_store(const std::string& log_dir,
                                   const jungle_log_store::Options& opt)
    : logDir(log_dir)
    , dummyLogEntry(cs_new<log_entry>(0, zero_buf, log_val_type::app_log))
    , dbInst(nullptr)
    , cacheInst( new log_cache(opt) )
    , flushThread(nullptr)
    , lastDurableLogIdx(0)
    , flushThreadStopSignal(false)
    , myOpt(opt)
{
    jungle::DBConfig db_config;
    db_config.allowOverwriteSeqNum = true;
    db_config.logSectionOnly = true;
    db_config.logFileTtl_sec = 60;
    db_config.compactionFactor = 0;
    db_config.maxEntriesInLogFile = myOpt.maxEntriesInLogFile;
    db_config.maxLogFileSize = myOpt.maxLogFileSize;
    db_config.maxKeepingMemtables = myOpt.maxKeepingMemtables;
    db_config.nextLevelExtension = false;

    db_config.compOpt.cbGetMaxSize =
        std::bind( &jungle_log_store::getCompMaxSize,
                   this,
                   std::placeholders::_1,
                   std::placeholders::_2 );
    db_config.compOpt.cbCompress =
        std::bind( &jungle_log_store::compress,
                   this,
                   std::placeholders::_1,
                   std::placeholders::_2,
                   std::placeholders::_3 );
    db_config.compOpt.cbDecompress =
        std::bind( &jungle_log_store::decompress,
                   this,
                   std::placeholders::_1,
                   std::placeholders::_2,
                   std::placeholders::_3 );

    jungle::Status s;
    s = jungle::DB::open(&dbInst, log_dir, db_config);
    assert(s.ok());

    if (myOpt.strongDurability) {
        flushThread = new std::thread(&jungle_log_store::flushLoop, this);
    }
}

jungle_log_store::~jungle_log_store() {
    close();
    DELETE(cacheInst);
}

ulong jungle_log_store::next_slot() const {
    static StatElem& lat = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_next_slot_latency" );
    GenericTimer tt;

    uint64_t _next_slot = cacheInst->nextSlot();
    if (!_next_slot) {
        // Cache is empty now.
        uint64_t max_seq = 0;
        jungle::Status s;
        s = dbInst->getMaxSeqNum(max_seq);
        if (!s) return 1;
        _next_slot = max_seq + 1;
    }

    lat += tt.getElapsedUs();
    return _next_slot;
}

ulong jungle_log_store::start_index() const {
    static StatElem& lat = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_start_index_latency" );
    GenericTimer tt;

    if (!cacheInst->getStartIndex()) {
        // In Jungle's perspective, min seqnum == last flushed seqnum + 1
        uint64_t min_seq = 0;
        jungle::Status s;
        s = dbInst->getMinSeqNum(min_seq);
        // start_index starts from 1.
        if (!s) return 1;
        cacheInst->setStartIndex( std::max((uint64_t)1, min_seq) );
    }
    lat += tt.getElapsedUs();

    return cacheInst->getStartIndex();
}

ptr<log_entry> jungle_log_store::last_entry() const {
    static StatElem& lat = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_last_entry_latency" );
    static StatElem& hit_cnt = *StatMgr::getInstance()->createStat
           ( StatElem::COUNTER,   "raft_log_cache_hit" );
    static StatElem& miss_cnt = *StatMgr::getInstance()->createStat
           ( StatElem::COUNTER,   "raft_log_cache_miss" );
    GenericTimer tt;

    uint64_t max_seq;
    jungle::Status s;
    s = dbInst->getMaxSeqNum(max_seq);
    if (!s) return dummyLogEntry;

    ptr<log_entry> ret = cacheInst->get(max_seq);
    if (!ret) {
        miss_cnt++;

        jungle::KV kv;
        s = dbInst->getSN(max_seq, kv);
        if (!s) return dummyLogEntry;

        SEBuf value_buf( kv.value.size, kv.value.data );
        ret = jl_dec_le(value_buf);
        kv.free();
    } else {
        hit_cnt++;
    }

    lat += tt.getElapsedUs();
    return ret;
}

ulong jungle_log_store::append(ptr<log_entry>& entry) {
    std::lock_guard<std::recursive_mutex> l(writeLock);

    uint64_t next_seq = 1;
    jungle::Status s;
    s = dbInst->getMaxSeqNum(next_seq);
    if (s) next_seq += 1;

    write_at_internal(next_seq, entry);
    return next_seq;
}

void jungle_log_store::rollback(ulong to) {
    dbInst->rollback(to);
    // Should drop cache.
    cacheInst->drop();
}

void jungle_log_store::write_at(ulong index, ptr<log_entry>& entry) {
    std::lock_guard<std::recursive_mutex> l(writeLock);

    uint64_t next_seq = 0;
    jungle::Status s;
    s = dbInst->getMaxSeqNum(next_seq);

    if (s && next_seq && next_seq > index) {
        // Overwrite log in the middle, rollback required before that.
        rollback(index - 1);
    }
    write_at_internal(index, entry);
}

void jungle_log_store::end_of_append_batch(ulong start, ulong cnt) {
    if (myOpt.strongDurability) {
        std::shared_ptr<FlushElem> my_req =
            std::make_shared<FlushElem>(start + cnt - 1);
        {
            std::lock_guard<std::mutex> l(flushReqsLock);
            flushReqs.push_back(my_req);
        }
        eaFlushThread.invoke();
        while (!my_req->done) {
            my_req->eaCaller.wait_ms(100);
        }
    }

    if (myOpt.cbEndOfBatch) {
        myOpt.cbEndOfBatch(start, cnt);
    }
}

void jungle_log_store::write_at_internal(ulong index, ptr<log_entry>& entry)
{
    static StatElem& write_lat = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_write_latency" );
    GenericTimer tt;

    std::lock_guard<std::recursive_mutex> l(writeLock);

    jungle::KV kv;
    std::string key_str = "seq" + std::to_string(index);
    kv.key.set(key_str);

    SEBuf value_buf = SEBuf::alloc(jl_calc_len(entry));
    SEBufSerializer ss(value_buf);
    SEBufHolder h(value_buf);

    jl_enc_le(entry, ss);
    kv.value.set(value_buf.size(), value_buf.data());

    jungle::Status s;
    s = dbInst->setSN(index, kv);
    (void)s; assert(s);

    cacheInst->set(index, entry);

    write_lat += tt.getElapsedUs();
}

ptr< std::vector< ptr<log_entry> > >
    jungle_log_store::log_entries_ext( ulong start,
                                       ulong end,
                                       int64_t size_hint )
{
    static StatElem& read_lat = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_read_latency" );
    static StatElem& read_dist = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_read_size_distribution" );
    static StatElem& hit_cnt = *StatMgr::getInstance()->createStat
           ( StatElem::COUNTER,   "raft_log_cache_hit" );
    static StatElem& miss_cnt = *StatMgr::getInstance()->createStat
           ( StatElem::COUNTER,   "raft_log_cache_miss" );
    GenericTimer tt;

    if (start >= end) {
        // Mostly for heartbeat.
        return nullptr;
    }

    read_dist += end - start;

    ptr< std::vector< ptr<log_entry> > > ret =
        cs_new< std::vector< ptr<log_entry> > >(end - start);

    bool cache_hit = cacheInst->gets(start, end, size_hint, ret);
    if (!cache_hit) {
        miss_cnt++;
        if (end <= 2 + start) {
            // In case of just 1-2 entries, point query is much lighter than iterator.
            for (ulong ii = start ; ii < end ; ++ii) {
                (*ret)[ii - start] = entry_at(ii);
            }
            read_lat += tt.getElapsedUs();
            return ret;
        }

        int64_t cur_size = 0;
        ulong idx = 0;
        jungle::Status s;
        jungle::Iterator itr;
        s = itr.initSN(dbInst, start, end - 1);
        assert(s);
        for (ulong ii = start; ii < end; ++ii) {
            jungle::Record rec;
            s = itr.get(rec);
            assert(s);

            SEBuf value_buf( rec.kv.value.size, rec.kv.value.data );
            ptr<log_entry> le = jl_dec_le(value_buf);
            (*ret)[idx++] = le;
            rec.free();

            if (!le->is_buf_null()) {
                cur_size += le->get_buf().container_size();
            }
            if (size_hint > 0 && cur_size >= size_hint) {
                ret->resize(idx);
                break;
            }

            s = itr.next();
            (void)s;
        }
        itr.close();

    } else {
        hit_cnt++;
    }

    read_lat += tt.getElapsedUs();
    return ret;
}

ptr<log_entry> jungle_log_store::entry_at(ulong index) {
    static StatElem& read_lat = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_entry_at_latency" );
    static StatElem& hit_cnt = *StatMgr::getInstance()->createStat
           ( StatElem::COUNTER,   "raft_log_cache_hit" );
    static StatElem& miss_cnt = *StatMgr::getInstance()->createStat
           ( StatElem::COUNTER,   "raft_log_cache_miss" );
    GenericTimer tt;

    ptr<log_entry> ret = cacheInst->get(index);
    if (ret) {
        hit_cnt++;

    } else {
        miss_cnt++;
        jungle::KV kv;
        jungle::Status s;
        s = dbInst->getSN(index, kv);
        if (!s) return dummyLogEntry;

        SEBuf value_buf( kv.value.size, kv.value.data );
        ret = jl_dec_le(value_buf);
        kv.free();
    }

    read_lat += tt.getElapsedUs();
    return ret;
}

ulong jungle_log_store::term_at(ulong index) {
    static StatElem& read_lat = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_term_at_latency" );
    GenericTimer tt;

    // NOTE: `term_at` will not update cache hit/miss count
    //       as it will be periodically invoked so that will spoil
    //       the hit ratio.

    uint64_t term = 0;
    ptr<log_entry> ret = cacheInst->get(index);
    if (ret) {
        term = ret->get_term();

    } else {
        jungle::KV kv;
        jungle::Status s;
        s = dbInst->getSN(index, kv);
        if (!s) return 0;

        SEBuf value_buf( kv.value.size, kv.value.data );
        SEBufSerializer ss(value_buf);
        term = ss.getU64();

        kv.free();
    }

    read_lat += tt.getElapsedUs();
    return term;
}

ptr<buffer> jungle_log_store::pack(ulong index, int32 cnt) {
    jungle::Iterator itr;
    jungle::Status s;
    s = itr.initSN(dbInst, index);
    if (!s) return nullptr;

    std::vector<jungle::Record> records(cnt);
    int idx = 0;
    do {
        jungle::Record& rec = records[idx];
        s = itr.get(rec);
        if (!s) break;
        assert(rec.seqNum == index + idx);

        idx++;
        if (idx >= cnt) break;

    } while (itr.next());
    assert(idx == cnt);

    itr.close();

    //   << Format >>
    // # records (N)        4 bytes
    // +---
    // | log length (X)     4 bytes
    // | log data           X bytes
    // +--- repeat N

    // Calculate size.
    uint64_t buf_size = sz_int;
    for (auto& entry: records) {
        jungle::Record& rec = entry;
        buf_size += sz_int;
        buf_size += rec.kv.value.size;
    }

    ptr<buffer> ret_buf = buffer::alloc(buf_size);

    // Put data
    ret_buf->put((int32)records.size());
    for (auto& entry: records) {
        jungle::Record& rec = entry;
        ret_buf->put(rec.kv.value.data, rec.kv.value.size);
        rec.free();
    }

    ret_buf->pos(0);
    return ret_buf;
}

void jungle_log_store::apply_pack(ulong index, buffer& pack) {
    jungle::Status s;
    pack.pos(0);

    size_t num = pack.get_int();
    for (size_t ii=0; ii<num; ++ii) {
        jungle::KV kv;
        std::string key_str = "seq" + std::to_string(index + ii);
        kv.key.set(key_str);

        size_t log_len;
        byte* ptr = const_cast<byte*>(pack.get_bytes(log_len));
        kv.value.set(log_len, ptr);

        s = dbInst->setSN(index + ii, kv);
        assert(s);
    }

    // Sync at once.
    s = dbInst->sync(true);
    (void)s;

    // Drop all contents in the cache.
    cacheInst->drop();
}

bool jungle_log_store::compact(ulong last_log_index) {
    static StatElem& cpt_lat = *StatMgr::getInstance()->createStat
           ( StatElem::HISTOGRAM, "raft_log_compact_latency" );
    GenericTimer tt;

    // append(), write_at(), and compact() are already protected by
    // Raft's lock, but add it here just in case.
    std::lock_guard<std::recursive_mutex> l(writeLock);

    jungle::Status s;

    uint64_t max_seq;
    s = dbInst->getMaxSeqNum(max_seq);
    if (!s || max_seq < last_log_index) {
        // This happens during snapshot sync.
        // Append a dummy log and then purge.
        jungle::KV kv;
        kv.set("dummy_key", "dummy_value");
        s = dbInst->setSN(last_log_index, kv);
        cacheInst->drop();
    }

    jungle::FlushOptions f_options;
    f_options.purgeOnly = true; // Do not flush to the table, but just purge.
    s = dbInst->sync(true);
    (void)s;
    s = dbInst->flushLogs(f_options, last_log_index);

    cacheInst->compact(last_log_index);

    // Reset cached start index number.
    // Next `start_index()` will synchronize it.
    cacheInst->setStartIndex(0);

    cpt_lat += tt.getElapsedUs();

    // Need to tolerate race condition.
    if (!s && s != jungle::Status::OPERATION_IN_PROGRESS) {
        return false;
    }
    return true;
}

bool jungle_log_store::flush() {
    if (dbInst) {
        jungle::Status s;
        dbInst->sync(true);

        uint64_t last_synced_idx = 0;
        s = dbInst->getLastSyncedSeqNum(last_synced_idx);
        if (s && last_synced_idx) {
            lastDurableLogIdx = last_synced_idx;
        }
    }
    return true;
}

void jungle_log_store::close() {
    // Should close flush thread first.
    flushThreadStopSignal = true;
    if (flushThread) {
        if (flushThread->joinable()) {
            eaFlushThread.invoke();
            flushThread->join();
        }
        DELETE(flushThread);
    }

    if (dbInst) {
        dbInst->sync(true);
        jungle::DB::close(dbInst);
        dbInst = nullptr;
    }
}

void jungle_log_store::shutdown() {
    jungle::DB::shutdown();
}

} // namespace

