///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//

#include <iostream>
#include <cassert>

#include <libnuraft/nuraft.hxx>
#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include "example_factory.h"

SDS_OPTION_GROUP(client, (add, "a", "add", "Add a server to the cluster", cxxopts::value< uint32_t >(), "id"),
                 (clean, "", "clean", "Reset all persistence", cxxopts::value< bool >(), ""),
                 (server, "", "server", "Server to send message to", cxxopts::value< uint32_t >()->default_value("0"),
                  "id"),
                 (echo, "m", "echo", "Send message to echo service", cxxopts::value< std::string >(), "message"),
                 (remove, "r", "remove", "Remove server from cluster", cxxopts::value< uint32_t >(), "id"))

SDS_OPTIONS_ENABLE(logging, client)
SDS_LOGGING_INIT(nuraft)

using namespace sds;

void cleanup(const std::string& prefix) { auto r = system(format(FMT_STRING("rm -rf {}"), prefix).data()); }

int send_message(uint32_t leader_id, std::string const& message) {
    auto       factory = std::make_shared< example_factory >(2, "raft_client");
    auto const dest_cfg = nuraft::srv_config(leader_id, std::to_string(leader_id));

    auto buf = nuraft::buffer::alloc(message.length() + 1);
    buf->put(message.c_str());
    buf->pos(0);

    int ret = nuraft::OK == factory->client_request(buf, dest_cfg).get() ? 0 : -1;
    sds::grpc::GrpcAyncClientWorker::shutdown_all();
    return ret;
}

int add_new_server(uint32_t leader_id, uint32_t srv_id) {
    auto       factory = std::make_shared< example_factory >(2, "raft_client");
    auto const dest_cfg = nuraft::srv_config(leader_id, std::to_string(leader_id));
    int ret = nuraft::OK == factory->add_server(srv_id, fmt::format(FMT_STRING("{}"), srv_id), dest_cfg).get() ? 0 : -1;
    sds::grpc::GrpcAyncClientWorker::shutdown_all();
    return ret;
}

int remove_server(int leader_id, int srv_id) {
    auto       factory = std::make_shared< example_factory >(2, "raft_client");
    auto const dest_cfg = nuraft::srv_config(leader_id, std::to_string(leader_id));
    int        ret = nuraft::OK == factory->rem_server(srv_id, dest_cfg).get() ? 0 : -1;
    sds::grpc::GrpcAyncClientWorker::shutdown_all();
    return ret;
}

int main(int argc, char** argv) {
    SDS_OPTIONS_LOAD(argc, argv, logging, client)

    // Can start using LOG from this point onward.
    sds_logging::SetLogger("raft_client");
    spdlog::set_pattern("[%D %T%z] [%n] [%^%l%$] [%t] %v");

    if (SDS_OPTIONS.count("clean")) {
        cleanup("*.config");
        cleanup("*_log");
        cleanup("*.state");
        return 0;
    }

    auto const server_id = SDS_OPTIONS["server"].as< uint32_t >();

    if (SDS_OPTIONS.count("echo")) {
        return send_message(server_id, SDS_OPTIONS["echo"].as< std::string >());
    } else if (SDS_OPTIONS.count("add")) {
        return add_new_server(server_id, SDS_OPTIONS["add"].as< uint32_t >());
    } else if (SDS_OPTIONS.count("remove")) {
        return remove_server(server_id, SDS_OPTIONS["remove"].as< uint32_t >());
    } else {
        std::cout << SDS_PARSER.help({}) << std::endl;
    }
    return 0;
}
