#include <iostream>
#include <cassert>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/grpc/rpc_client.hpp>
#include <sisl/utility/thread_buffer.hpp>
#include <nuraft_mesg/mesg_factory.hpp>

#include "uuids.h"

SISL_OPTION_GROUP(client, (add, "a", "add", "Add a server to the cluster", cxxopts::value< uint32_t >(), "id"),
                  (clean, "", "clean", "Reset all persistence", cxxopts::value< bool >(), ""),
                  (group, "g", "group", "Group ID", cxxopts::value< std::string >(), "id"),
                  (server, "", "server", "Server to send message to", cxxopts::value< uint32_t >()->default_value("0"),
                   "id"),
                  (echo, "m", "echo", "Send message to echo service", cxxopts::value< std::string >(), "message"),
                  (remove, "r", "remove", "Remove server from cluster", cxxopts::value< uint32_t >(), "id"))

SISL_OPTIONS_ENABLE(logging, client)
SISL_LOGGING_INIT(nuraft, nuraft_mesg, httpserver_lmod, grpc_server)

void cleanup(const std::string& prefix) { auto r = system(format(FMT_STRING("rm -rf {}"), prefix).data()); }

using nuraft_mesg::mesg_factory;
using namespace nuraft;

struct example_factory : public nuraft_mesg::group_factory {
    example_factory(int const threads, std::string const& name) :
            nuraft_mesg::group_factory::group_factory(threads, name, nullptr) {}

    std::string lookupEndpoint(std::string const& client) override {
        for (auto i = 0u; i < 5; ++i) {
            if (uuids[i] == client) { return format(FMT_STRING("127.0.0.1:{}"), 9000 + i); }
        }
        return client;
    }
};

int send_message(uint32_t leader_id, std::string const& group_id, std::string const& message) {
    auto g_factory = std::make_shared< example_factory >(2, group_id);
    auto factory = std::make_shared< mesg_factory >(g_factory, group_id, "test_package");
    auto const dest_cfg = srv_config(leader_id, uuids[stol(group_id) + leader_id]);

    auto buf = buffer::alloc(message.length() + 1);
    buf->put(message.c_str());
    buf->pos(0);

    nuraft::cmd_result_code rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        rc = factory->client_request(buf, dest_cfg).get();
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    int ret = nuraft::OK == rc ? 0 : -1;
    sisl::GrpcAsyncClientWorker::shutdown_all();
    return ret;
}

int add_new_server(uint32_t leader_id, uint32_t srv_id, std::string const& group_id) {
    auto g_factory = std::make_shared< example_factory >(2, group_id);
    auto factory = std::make_shared< mesg_factory >(g_factory, group_id, "test_package");
    auto const dest_cfg = srv_config(leader_id, uuids[stol(group_id) + leader_id]);

    nuraft::cmd_result_code rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        rc = factory->add_server(srv_id, uuids[srv_id], dest_cfg).get();
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    int ret = nuraft::OK == rc ? 0 : -1;
    sisl::GrpcAsyncClientWorker::shutdown_all();
    return ret;
}

int remove_server(uint32_t leader_id, std::string const& group_id, uint32_t srv_id) {
    auto g_factory = std::make_shared< example_factory >(2, group_id);
    auto factory = std::make_shared< mesg_factory >(g_factory, group_id, "test_package");
    auto const dest_cfg = srv_config(leader_id, uuids[stol(group_id) + leader_id]);

    nuraft::cmd_result_code rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        rc = factory->rem_server(srv_id, dest_cfg).get();
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    int ret = nuraft::OK == rc ? 0 : -1;
    sisl::GrpcAsyncClientWorker::shutdown_all();
    return ret;
}

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, logging, client)

    // Can start using LOG from this point onward.
    sisl::logging::SetLogger("raft_client");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    if (SISL_OPTIONS.count("clean")) {
        cleanup("group*");
        cleanup("jungle*");
        cleanup("server_*");
        cleanup("*_log");
        return 0;
    }

    auto const group_id = SISL_OPTIONS["group"].as< std::string >();
    auto const server_id = SISL_OPTIONS["server"].as< uint32_t >();

    if (SISL_OPTIONS.count("echo")) {
        return send_message(server_id, group_id, SISL_OPTIONS["echo"].as< std::string >());
    } else if (SISL_OPTIONS.count("add")) {
        return add_new_server(server_id, SISL_OPTIONS["add"].as< uint32_t >(), group_id);
    } else if (SISL_OPTIONS.count("remove")) {
        return remove_server(server_id, group_id, SISL_OPTIONS["remove"].as< uint32_t >());
    } else {
        std::cout << SISL_PARSER.help({}) << std::endl;
    }
    return 0;
}
