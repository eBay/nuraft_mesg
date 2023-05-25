#pragma once

#include <libnuraft/nuraft.hxx>
#include <libjungle/jungle.h>
#include "event_awaiter.h"

#include <memory>
#include <thread>

namespace nuraft {

class jungle_log_store : public log_store {
public:
    struct Options {
        Options()
            : maxEntriesInLogFile(64*1024)
            , maxLogFileSize(32*1024*1024)
            , maxKeepingMemtables(8)
            , maxCacheSizeBytes((uint64_t)512 * 1024 * 1024)
            , maxCachedLogs(10000)
            , compression(false)
            , strongDurability(false)
            , flushThreadSleepTimeUs(500)
            , cbEndOfBatch(nullptr)
            {}
        uint64_t maxEntriesInLogFile;
        uint64_t maxLogFileSize;
        uint64_t maxKeepingMemtables;
        uint64_t maxCacheSizeBytes;
        uint64_t maxCachedLogs;
        bool compression;

        /**
         * If `true`, all dirty Raft logs are flushed
         * for the end of each batch.
         */
        bool strongDurability;

        /**
         * If non-zero, we will enable a periodic
         * flush thread, whose sleep time will be this value
         * in micro-seconds.
         */
        uint32_t flushThreadSleepTimeUs;

        /**
         * Callback function that is called at the end of
         * `end_of_append_batch` function.
         */
        std::function< void(uint64_t, uint64_t) > cbEndOfBatch;
    };

    jungle_log_store(const std::string& log_dir,
                     const Options& opt = jungle_log_store::Options());
    ~jungle_log_store();

    __nocopy__(jungle_log_store);

public:
    /**
     * The first available slot of the store, starts with 1.
     *
     * @return Last log index number + 1
     */
    virtual ulong next_slot() const;

    /**
     * The start index of the log store, at the very beginning, it must be 1.
     * However, after some compact actions, this could be anything
     * greater or equals to one.
     *
     * @return Starting log index number.
     */
    virtual ulong start_index() const;

    /**
     * The last log entry in store.
     *
     * @return If no log entry exists: a dummy constant entry with
     *         value set to null and term set to zero.
     */
    virtual ptr<log_entry> last_entry() const;

    /**
     * Append a log entry to store
     *
     * @param entry Log entry
     * @return Log index number.
     */
    virtual ulong append(ptr<log_entry>& entry);

    /**
     * Overwrite a log entry at the given `index`.
     *
     * @param index Log index number to overwrite.
     * @param entry New log entry to overwrite.
     */
    virtual void write_at(ulong index, ptr<log_entry>& entry);

    /**
     * Invoked after a batch of logs is written as a part of
     * a single append_entries request.
     *
     * @param start The start log index number (inclusive)
     * @param cnt The number of log entries written.
     */
    virtual void end_of_append_batch(ulong start, ulong cnt);

    /**
     * Get log entries with index [start, end).
     *
     * Return nullptr to indicate error if any log entry within the requested range
     * could not be retrieved (e.g. due to external log truncation).
     *
     * @param start The start log index number (inclusive).
     * @param end The end log index number (exclusive).
     * @return The log entries between [start, end).
     */
    virtual ptr<std::vector<ptr<log_entry>>> log_entries(ulong start, ulong end) {
        return log_entries_ext(start, end);
    }

    /**
     * (Optional)
     * Get log entries with index [start, end).
     *
     * The total size of the returned entries is limited by batch_size_hint.
     *
     * Return nullptr to indicate error if any log entry within the requested range
     * could not be retrieved (e.g. due to external log truncation).
     *
     * @param start The start log index number (inclusive).
     * @param end The end log index number (exclusive).
     * @param batch_size_hint_in_bytes Total size (in bytes) of the returned entries,
     *        see the detailed comment at
     *        `state_machine::get_next_batch_size_hint_in_bytes()`.
     * @return The log entries between [start, end) and limited by the total size
     *         given by the batch_size_hint_in_bytes.
     */
    virtual ptr<std::vector<ptr<log_entry>>> log_entries_ext(
        ulong start,
        ulong end,
        int64_t batch_size_hint_in_bytes = 0);

    /**
     * Get the log entry at the specified log index number.
     *
     * @param index Should be equal to or greater than 1.
     * @return The log entry or null if index >= this->next_slot().
     */
    virtual ptr<log_entry> entry_at(ulong index);

    /**
     * Get the term for the log entry at the specified index
     * Suggest to stop the system if the index >= this->next_slot()
     *
     * @param index Should be equal to or greater than 1.
     * @return The term for the specified log entry, or
     *         0 if index < this->start_index().
     */
    virtual ulong term_at(ulong index);

    /**
     * Pack cnt log items starts from index
     *
     * @param index The start log index number (inclusive).
     * @param cnt The number of logs to pack.
     * @return log pack
     */
    virtual ptr<buffer> pack(ulong index, int32 cnt);

    /**
     * Apply the log pack to current log store, starting from index.
     *
     * @param index The start log index number (inclusive).
     * @param pack
     */
    virtual void apply_pack(ulong index, buffer& pack);

    /**
     * Compact the log store by purging all log entries,
     * including the log at the last_log_index.
     *
     * If current max log idx is smaller than given `last_log_index`,
     * set start log idx to `last_log_index + 1`.
     *
     * @param last_log_index Log index number that will be purged up to (inclusive).
     * @return True on success.
     */
    virtual bool compact(ulong last_log_index);

    /**
     * Synchronously flush all log entries in this log store to the backing storage
     * so that all log entries are guaranteed to be durable upon process crash.
     *
     * @return `true` on success.
     */
    virtual bool flush();

    /**
     * Close the log store. It will make all dirty data durable on disk
     * before closing.
     */
    void close();

    /**
     * Rollback log store to given index number.
     *
     * @param to Log index number that will be the last log after rollback,
     *           that means the log corresponding to `to` will be preserved.
     * @return void.
     */
    void rollback(ulong to);

    /**
     * Free all resources used for Jungle.
     */
    static void shutdown();

private:
    struct log_cache;

    struct FlushElem;

    void write_at_internal(ulong index, ptr<log_entry>& entry);

    ssize_t getCompMaxSize(jungle::DB* db,
                           const jungle::Record& rec);

    ssize_t compress(jungle::DB* db,
                     const jungle::Record& src,
                     jungle::SizedBuf& dst);

    ssize_t decompress(jungle::DB* db,
                       const jungle::SizedBuf& src,
                       jungle::SizedBuf& dst);

    void flushLoop();

    /**
     * Directory path.
     */
    std::string logDir;

    /**
     * Dummy log entry for invalid request.
     */
    ptr<log_entry> dummyLogEntry;

    /**
     * Jungle is basically lock-free for both read & write,
     * but use write lock to be safe.
     */
    std::recursive_mutex writeLock;

    /**
     * DB instance.
     */
    jungle::DB *dbInst;

    /**
     * Log cache instance.
     */
    log_cache* cacheInst;

    /**
     * Flush thread instance.
     */
    std::thread* flushThread;

    /**
     * To invoke flush thread.
     */
    EventAwaiter eaFlushThread;

    /**
     * List of awaiting flush requests.
     */
    std::list< std::shared_ptr<FlushElem> > flushReqs;

    /**
     * Mutex for `flushReqs`.
     */
    std::mutex flushReqsLock;

    /**
     * The index number of the last durable Raft log.
     */
    std::atomic<uint64_t> lastDurableLogIdx;

    /**
     * `true` if we are stopping flush thraed.
     */
    std::atomic<bool> flushThreadStopSignal;

    /**
     * Local copy of options.
     */
    Options myOpt;
};

}

