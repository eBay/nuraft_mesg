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

#include "cornerstone/raft_core_grpc.hpp"
#include <sds_logging/logging.h>
#include <iostream>
#include <cassert>
#include <chrono>

#include <stdlib.h>

SDS_LOGGING_INIT

using namespace cornerstone;

extern void cleanup(const std::string& folder);

ptr<grpc_service> grpc_svc_;
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

void test_raft_server_with_grpc(int srv_id, char* args) {
    grpc_svc_ = cs_new<grpc_service>();
    ptr<rpc_client> client(grpc_svc_->create_client(
            sstrfmt("127.0.0.1:900%d").fmt(srv_id) ));
    ptr<req_msg> msg = cs_new<req_msg>(0, msg_type::client_request, 0, 1, 0, 0, 0);

    leader_id_global = srv_id;

    ptr<buffer> buf = buffer::alloc(strlen(args)+1);
    buf->put(args);
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
    grpc_svc_ = cs_new<grpc_service>();
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
    grpc_svc_ = cs_new<grpc_service>();
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
    sds_logging::SetLogger(spdlog::stdout_color_mt("raft_client"));
    if (argc == 5 &&
        !strcmp(argv[1], "-l") &&
        !strcmp(argv[3], "-m")) {
        int srv_id = atoi(argv[2]);
        test_raft_server_with_grpc(srv_id, argv[4]);
        return 0;

    } else if (argc == 3 && !strcmp(argv[1], "-m")) {
        test_raft_server_with_grpc(1, argv[2]);
        return 0;

    } else if (argc == 2 && !strcmp(argv[1], "-c")) {
        cleanup("store*");
        cleanup("log*.log");
        return 0;

    } else if (argc == 3 && !strcmp(argv[1], "-a")) {
        int srv_id = atoi(argv[2]);
        add_new_server(1, srv_id);
        return 0;

    } else if (argc == 3 && !strcmp(argv[1], "-r")) {
        int srv_id = atoi(argv[2]);
        remove_server(1, srv_id);
        return 0;

    }

    printf("usage: client [-l <ID>] [-m <message>] [-c] [-a <ID>]\n");
    printf("    -l      ID of server to send message (default: 1).\n");
    printf("    -m      Send message to echo server.\n");
    printf("    -c      Cleanup all logs and data.\n");
    printf("    -a      Add new server to the cluster.\n");
    printf("    -r      Remove existing server from the cluster.\n");
    printf("\n");
    printf("examples:\n");
    printf("    $ ./client -c               Cleanup.\n");
    printf("    $ ./client -m test          Send a message 'test'.\n");
    printf("    $ ./client -l 2 -m test     Send a message 'test' to server 2.\n");
    printf("    $ ./client -a 4             Add a new server 4 to the existing cluster.\n");
    printf("    $ ./client -r 4             Remove server 4 from the cluster.\n");

    return 0;
}
