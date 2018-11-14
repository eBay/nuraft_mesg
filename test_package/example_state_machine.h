#pragma once

#include <cornerstone.hxx>
#include <sds_logging/logging.h>

using namespace cornerstone;

class echo_state_machine : public state_machine {
 public:
   echo_state_machine() : lock_(), last_commit_idx_(0) {}
 public:
   virtual ptr<buffer> commit(const ulong log_idx, buffer& data) {
      auto_lock(lock_);

      LOGINFO("Commit message [{}] : {}", log_idx, reinterpret_cast<const char*>(data.data()));
      last_commit_idx_ = log_idx;
      return nullptr;
   }

   virtual ptr<buffer> pre_commit(const ulong log_idx, buffer& data) {
      auto_lock(lock_);
      LOGINFO("Pre-Commit message [{}] : {}", log_idx, reinterpret_cast<const char*>(data.data()));
      return nullptr;
   }

   virtual void rollback(const ulong log_idx, buffer& data) {
      auto_lock(lock_);
      LOGINFO("Rollback[{}] : {}", log_idx, reinterpret_cast<const char*>(data.data()));
   }

   virtual void save_snapshot_data(snapshot& s, const ulong offset, buffer& data) {}
   virtual bool apply_snapshot(snapshot& s) {
      return true;
   }

   virtual int read_snapshot_data(snapshot& s, const ulong offset, buffer& data) {
      return 0;
   }

   virtual ptr<snapshot> last_snapshot() {
      return ptr<snapshot>();
   }

   virtual void create_snapshot(snapshot& s, async_result<bool>::handler_type& when_done) {}

   virtual ulong last_commit_index() { return last_commit_idx_; }

 private:
   std::mutex lock_;
   ulong last_commit_idx_;
};

