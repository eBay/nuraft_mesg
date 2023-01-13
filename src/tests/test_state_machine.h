/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#include <libnuraft/nuraft.hxx>
#include <sisl/logging/logging.h>

using namespace nuraft;

auto unwrap_buffer(nuraft::buffer& data) {
    size_t buffer_len{0};
    auto const buffer_begin = data.get_bytes(buffer_len);
    auto const buffer_end = buffer_begin + buffer_len;
    auto input = std::vector< uint8_t >(buffer_begin, buffer_end);
    return nlohmann::json::from_msgpack(std::move(input));
}

class test_state_machine : public state_machine {
public:
    test_state_machine() : lock_(), last_commit_idx_(0) {}

public:
    virtual ptr< buffer > commit(const ulong log_idx, buffer& data) {
        auto_lock(lock_);

        auto j_obj = unwrap_buffer(data);
        LOGINFO("Commit message [{}] op: {}", log_idx, j_obj.at("op_type").get< int >());
        last_commit_idx_ = log_idx;
        return nullptr;
    }

    virtual ptr< buffer > pre_commit(const ulong log_idx, buffer& data) {
        auto_lock(lock_);
        auto j_obj = unwrap_buffer(data);
        LOGINFO("Pre-Commit message [{}] op: {}", log_idx, j_obj.at("op_type").get< int >());
        return nullptr;
    }

    virtual void rollback(const ulong log_idx, buffer& data) {
        auto_lock(lock_);
        auto j_obj = unwrap_buffer(data);
        LOGINFO("Rollback message [{}] op: {}", log_idx, j_obj.at("op_type").get< int >());
    }

    virtual void save_snapshot_data(snapshot& s, const ulong offset, buffer& data) {}
    virtual bool apply_snapshot(snapshot& s) { return true; }

    virtual int read_snapshot_data(snapshot& s, const ulong offset, buffer& data) { return 0; }

    virtual ptr< snapshot > last_snapshot() { return ptr< snapshot >(); }

    virtual void create_snapshot(snapshot& s, async_result< bool >::handler_type& when_done) {}

    virtual ulong last_commit_index() {
        auto_lock(lock_);
        return last_commit_idx_;
    }

private:
    std::mutex lock_;
    ulong last_commit_idx_;
};
