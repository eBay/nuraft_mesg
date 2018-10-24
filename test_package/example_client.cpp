/**
*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  The ASF licenses
* this file to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <iostream>
#include <cassert>

#include <cornerstone/raft_core_grpc.hpp>
#include <sds_logging/logging.h>
#include <example_service.grpc.pb.h>
#include "example_factory.h"

SDS_OPTION_GROUP(client, (add, "a", "add", "Add a server to the cluster", cxxopts::value<uint32_t>(), "id"),
                         (clean, "", "clean", "Reset all persistence", cxxopts::value<bool>(), ""),
                         (server, "", "server", "Server to send message to", cxxopts::value<uint32_t>()->default_value("0"), "id"),
                         (echo, "m","echo", "Send message to echo service", cxxopts::value<std::string>(), "message"),
                         (remove, "r","remove", "Remove server from cluster", cxxopts::value<uint32_t>(), "id"))

SDS_OPTIONS_ENABLE(logging, client)
SDS_LOGGING_INIT()
using namespace cornerstone;

extern void cleanup(const std::string& folder);

ptr<example_factory> grpc_svc_;
bool message_complete {false};
std::mutex stop_test_lock1;
std::condition_variable stop_test_cv1;

void cleanup(const std::string& prefix) {
    auto r = system(format(fmt("rm -rf {}"), prefix).data());
}

int32_t leader_id_global;
ptr<req_msg> msg_global;

void handle_resp_ctx(ptr<resp_msg>& rsp) {
    auto ctx = rsp->get_ctx();
    if (ctx) {
        ctx->pos(0);
        LOGINFO("Got response: {}", reinterpret_cast<const char*>(ctx->data()));
    }
    { std::lock_guard<std::mutex> lk (stop_test_lock1);
      message_complete = true;
    }
    stop_test_cv1.notify_all();
}

void resp_handler_indirect(ptr<resp_msg>& rsp, ptr<rpc_exception>& err) {
    assert(rsp->get_accepted());
    handle_resp_ctx(rsp);
}

void resp_handler(ptr<resp_msg>& rsp, ptr<rpc_exception>& err) {
    if (err) {
        LOGERROR("{}", err->what());
        { std::lock_guard<std::mutex> lk(stop_test_lock1);
          message_complete = true;
        }
        stop_test_cv1.notify_all();
        return;
    }

    // Should be either accepted or forwarded.
    assert(rsp->get_accepted() || rsp->get_dst() > 0);

    if (rsp->get_accepted()) {
       handle_resp_ctx(rsp);
       return;
    }

    // Not accepted: means that `get_dst()` is a new leader.
    // Forward the message.
    LOGINFO("{} is not leader now, current leader: {}", leader_id_global, rsp->get_dst());
    auto client_new = grpc_svc_->create_client(std::to_string(rsp->get_dst()));

    auto handler = static_cast<rpc_handler>(resp_handler_indirect);
    client_new->send(msg_global, handler);
    { // wait for response.
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l, [] {return message_complete;});
    }
    return;
}

void test_raft_server_with_grpc(uint32_t srv_id, std::string const& message) {
    grpc_svc_ = cs_new<example_factory>();
    auto client = grpc_svc_->create_client(std::to_string(srv_id));
    auto msg = cs_new<req_msg>(0, msg_type::client_request, 0, 1, 0, 0, 0);

    leader_id_global = srv_id;

    auto buf = buffer::alloc(message.length()+1);
    buf->put(message.c_str());
    buf->pos(0);
    msg->log_entries().push_back(cs_new<log_entry>(0, buf));
    msg_global = msg;

    auto handler = static_cast<rpc_handler>(resp_handler);
    client->send(msg, handler);

    { // wait for response.
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l, [] {return message_complete;});
    }
}

void add_new_server(uint32_t leader_id, uint32_t srv_id) {
    grpc_svc_ = cs_new<example_factory>();
    auto client = grpc_svc_->create_client(std::to_string(leader_id));

    leader_id_global = leader_id;

    auto srv_addr = std::to_string(srv_id);
    srv_config srv_conf(srv_id, srv_addr);
    auto log = cs_new<log_entry>(0, srv_conf.serialize(), log_val_type::cluster_server);
    auto msg = cs_new<req_msg>(0, msg_type::add_server_request, 0, 0, 0, 0, 0);
    msg->log_entries().push_back(log);
    msg_global = msg;

    auto handler = static_cast<rpc_handler>(resp_handler);
    client->send(msg, handler);

    { // wait for response.
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l, [] {return message_complete;});
    }
}

void remove_server(int leader_id, int srv_id) {
    grpc_svc_ = cs_new<example_factory>();
    auto client = grpc_svc_->create_client(std::to_string(leader_id));

    leader_id_global = leader_id;

    auto buf = buffer::alloc(sz_int);
    buf->put(static_cast<int32_t>(srv_id));
    buf->pos(0);
    auto log = cs_new<log_entry>(0, buf, log_val_type::cluster_server);
    auto msg = cs_new<req_msg>(0, msg_type::remove_server_request, 0, 0, 0, 0, 0);
    msg->log_entries().push_back(log);
    msg_global = msg;

    auto handler = static_cast<rpc_handler>(resp_handler);
    client->send(msg, handler);

    { // wait for response.
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l, [] {return message_complete;});
    }
}

int main(int argc, char** argv) {
    SDS_OPTIONS_LOAD(argc, argv, logging, client)

    // Can start using LOG from this point onward.
    sds_logging::SetLogger("raft_client");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    if (SDS_OPTIONS.count("clean")) {
        cleanup("*.config");
        cleanup("*_log");
        cleanup("*.state");
        return 0;
    }

    auto const server_id = SDS_OPTIONS["server"].as<uint32_t>();

    if (SDS_OPTIONS.count("echo")) {
       test_raft_server_with_grpc(server_id, SDS_OPTIONS["echo"].as<std::string>());
    } else if (SDS_OPTIONS.count("add")) {
        add_new_server(server_id, SDS_OPTIONS["add"].as<uint32_t>());

    } else if (SDS_OPTIONS.count("remove")) {
        remove_server(server_id, SDS_OPTIONS["remove"].as<uint32_t>());
    } else {
        std::cout << SDS_PARSER.help({}) << std::endl;
    }
    return 0;
}
