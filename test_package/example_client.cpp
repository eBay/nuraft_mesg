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
#include <chrono>
#include <cxxopts/cxxopts.hpp>

#include <cornerstone/raft_core_grpc.hpp>
#include <example_service.grpc.pb.h>
#include "example_factory.h"

using namespace cornerstone;

extern void cleanup(const std::string& folder);

ptr<example_factory> grpc_svc_;
bool message_complete {false};
std::mutex stop_test_lock1;
std::condition_variable stop_test_cv1;

void cleanup(const std::string& prefix) {
    int r;
    (void)r;
    std::string command = "rm -rf " + prefix;
    r = system(command.c_str());
}

void cleanup() {
    cleanup(".");
}

int leader_id_global;
ptr<req_msg> msg_global;

void handle_resp_ctx(ptr<resp_msg>& rsp) {
    ptr<buffer> ctx = rsp->get_ctx();
    if (ctx) {
        ctx->pos(0);
        std::cout << "got response message: "
                  << reinterpret_cast<const char*>(ctx->data()) << std::endl;
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
        std::cout << err->what() << std::endl;
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
    printf("%d is not leader now, current leader: %d\n",
           leader_id_global, rsp->get_dst());
    ptr<rpc_client> client_new(
            grpc_svc_->create_client(
                    sstrfmt("127.0.0.1:900%d").fmt(rsp->get_dst()))
                    );

    rpc_handler handler = (rpc_handler)(resp_handler_indirect);
    client_new->send(msg_global, handler);
    { // wait for response.
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l, [] {return message_complete;});
    }
    return;
}

void test_raft_server_with_grpc(int srv_id, std::string const& args) {
    grpc_svc_ = cs_new<example_factory>();
    ptr<rpc_client> client(grpc_svc_->create_client(
            sstrfmt("127.0.0.1:900%d").fmt(srv_id) ));
    ptr<req_msg> msg = cs_new<req_msg>(0, msg_type::client_request, 0, 1, 0, 0, 0);

    leader_id_global = srv_id;

    ptr<buffer> buf = buffer::alloc(args.length()+1);
    buf->put(args.data());
    buf->pos(0);
    msg->log_entries().push_back(cs_new<log_entry>(0, buf));
    msg_global = msg;

    std::chrono::time_point<std::chrono::system_clock> start, cur;
    start = std::chrono::system_clock::now();

    rpc_handler handler = (rpc_handler)(resp_handler);
    client->send(msg, handler);

    { // wait for response.
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l, [] {return message_complete;});
    }

    cur = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed = cur - start;
    printf("%.1f us elapsed\n", elapsed.count() * 1000000);
}

void add_new_server(int leader_id, int srv_id) {
    grpc_svc_ = cs_new<example_factory>();
    ptr<rpc_client> client(grpc_svc_->create_client(
            sstrfmt("127.0.0.1:900%d").fmt(leader_id) ));

    leader_id_global = leader_id;

    std::string srv_addr = "127.0.0.1:900" + std::to_string(srv_id);
    srv_config srv_conf(srv_id, srv_addr);
    ptr<buffer> buf(srv_conf.serialize());
    ptr<log_entry> log(cs_new<log_entry>(0, buf, log_val_type::cluster_server));
    ptr<req_msg> msg(cs_new<req_msg>(
            (ulong)0, msg_type::add_server_request, 0, 0,
            (ulong)0, (ulong)0, (ulong)0 ));
    msg->log_entries().push_back(log);
    msg_global = msg;

    std::chrono::time_point<std::chrono::system_clock> start, cur;
    start = std::chrono::system_clock::now();

    rpc_handler handler = (rpc_handler)(resp_handler);
    client->send(msg, handler);

    { // wait for response.
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l, [] {return message_complete;});
    }

    cur = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed = cur - start;
    printf("%.1f us elapsed\n", elapsed.count() * 1000000);
}

void remove_server(int leader_id, int srv_id) {
    grpc_svc_ = cs_new<example_factory>();
    ptr<rpc_client> client(grpc_svc_->create_client(
            sstrfmt("127.0.0.1:900%d").fmt(leader_id) ));

    leader_id_global = leader_id;

    ptr<buffer> buf(buffer::alloc(sz_int));
    buf->put(srv_id);
    buf->pos(0);
    ptr<log_entry> log(cs_new<log_entry>(0, buf, log_val_type::cluster_server));
    ptr<req_msg> msg(cs_new<req_msg>(
            (ulong)0, msg_type::remove_server_request, 0, 0,
            (ulong)0, (ulong)0, (ulong)0 ));
    msg->log_entries().push_back(log);
    msg_global = msg;

    std::chrono::time_point<std::chrono::system_clock> start, cur;
    start = std::chrono::system_clock::now();

    rpc_handler handler = (rpc_handler)(resp_handler);
    client->send(msg, handler);

    { // wait for response.
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l, [] {return message_complete;});
    }

    cur = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed = cur - start;
    printf("%.1f us elapsed\n", elapsed.count() * 1000000);
}

int main(int argc, char** argv) {
    cxxopts::Options options(argv[0], "Raft Client");
    options.add_options()
          ("h,help", "Help message")
          ("a,add", "Add a server to the cluster", cxxopts::value<uint32_t>(), "id")
          ("c,clean", "Reset all persistence")
          ("l,server", "Server to send message to", cxxopts::value<uint32_t>(), "id")
          ("m,echo", "Send message to echo service", cxxopts::value<std::string>(), "message")
          ("r,remove", "Remove server from cluster", cxxopts::value<uint32_t>(), "id");
    options.parse(argc, argv);

    if (options.count("help")) {
        std::cout << options.help({}) << std::endl;
        return 0;
    }

    if (options.count("clean")) {
        cleanup("store*");
        cleanup("log*.log");
        return 0;
    }

    auto server_id = 1u;
    if (options.count("server")) {
        server_id = options["server"].as<uint32_t>();
    }

    if (options.count("echo")) {
       test_raft_server_with_grpc(server_id, options["echo"].as<std::string>());
    } else if (options.count("add")) {
        add_new_server(server_id, options["add"].as<uint32_t>());

    } else if (options.count("remove")) {
        remove_server(server_id, options["remove"].as<uint32_t>());
    } else {
        std::cout << options.help({}) << std::endl;
    }
    return 0;
}
