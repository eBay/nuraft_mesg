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

#include "nuraft_mesg/mesg_state_mgr.hpp"
#include <sisl/logging/logging.h>

class test_state_machine;

namespace nuraft_mesg {
class Manager;
class service;
} // namespace nuraft_mesg

class test_state_mgr : public nuraft_mesg::mesg_state_mgr {
    bool _will_destroy{false};

public:
    test_state_mgr(int32_t srv_id, nuraft_mesg::peer_id_t const& srv_addr, nuraft_mesg::group_id_t const& group_id);
    ~test_state_mgr() override;

    nuraft::ptr< nuraft::cluster_config > load_config() override;
    void save_config(const nuraft::cluster_config& config) override;
    void save_state(const nuraft::srv_state& state) override;
    nuraft::ptr< nuraft::srv_state > read_state() override;
    nuraft::ptr< nuraft::log_store > load_log_store() override;
    int32_t server_id() override { return _srv_id; }

    void system_exit(const int exit_code) override { LOGINFO("System exiting with code [{}]", exit_code); }

    uint32_t get_logstore_id() const override;
    std::shared_ptr< nuraft::state_machine > get_state_machine() override;
    void permanent_destroy() override;
    void leave() override;

    ///// data service helper apis
    nuraft_mesg::AsyncResult< sisl::GenericClientResponse >
    data_service_request_bidirectional(nuraft_mesg::destination_t const& dest, std::string const& request_name,
                                       nuraft_mesg::io_blob_list_t const& cli_buf);
    nuraft_mesg::NullAsyncResult data_service_request_unidirectional(nuraft_mesg::destination_t const& dest,
                                                                     std::string const& request_name,
                                                                     nuraft_mesg::io_blob_list_t const& cli_buf);

    bool register_data_service_apis(nuraft_mesg::Manager* messaging);
    static void fill_data_vec(nuraft_mesg::io_blob_list_t& cli_buf, uint32_t size_bytes);
    static void fill_data_vec_big(nuraft_mesg::io_blob_list_t& cli_buf, uint32_t size_bytes);
    static uint16_t get_random_num();
    static uint32_t get_server_counter();
    static void verify_data(sisl::io_blob const& buf);
    nuraft_mesg::repl_service_ctx* get_repl_context() { return m_repl_svc_ctx.get(); }

private:
private:
    int32_t const _srv_id;
    nuraft_mesg::peer_id_t const _srv_addr;
    nuraft_mesg::group_id_t const _group_id;
    std::shared_ptr< test_state_machine > _state_machine;

    inline static std::atomic< uint32_t > server_counter{0};
    static std::vector< uint32_t > data_vec;
    inline static std::string const SEND_DATA{"send_data"};
    inline static std::string const REQUEST_DATA{"request_data"};
};
