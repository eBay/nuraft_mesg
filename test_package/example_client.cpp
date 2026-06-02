#include <expected>
#include <iostream>
#include <cassert>

#include <boost/uuid/string_generator.hpp>
#include <libnuraft/async.hxx>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/grpc/rpc_client.hpp>
#include <sisl/utility/thread_buffer.hpp>
#include <nuraft_mesg/mesg_factory.hpp>
#include <stdexec/execution.hpp>

#include <system_error>

#include "uuids.h"

SISL_OPTION_GROUP(client, (add, "a", "add", "Add a server to the cluster", cxxopts::value< uint32_t >(), "id"),
                  (clean, "", "clean", "Reset all persistence", cxxopts::value< bool >(), ""),
                  (group, "g", "group", "Group ID", cxxopts::value< uint32_t >()->default_value("0"), ""),
                  (server, "", "server", "Server to send message to", cxxopts::value< uint32_t >()->default_value("0"),
                   "id"),
                  (echo, "m", "echo", "Send message to echo service", cxxopts::value< std::string >(), "message"),
                  (remove, "r", "remove", "Remove server from cluster", cxxopts::value< uint32_t >(), "id"))

SISL_OPTIONS_ENABLE(logging, client)
SISL_LOGGING_INIT(nuraft_mesg, httpserver_lmod, grpc_server)

void cleanup(const std::string& prefix) { auto r = system(fmt::format(FMT_STRING("rm -rf {}"), prefix).data()); }

using nuraft_mesg::mesg_factory;
using namespace nuraft;

// The factory control calls are coroutines (null_async_task); a non-coroutine client drives one to
// completion with stdexec::sync_wait.
static nuraft_mesg::null_result sync_get(nuraft_mesg::null_async_task task) {
    auto done = stdexec::sync_wait(std::move(task));
    if (!done) { return std::unexpected(std::make_error_condition(std::errc::operation_canceled)); }
    return std::get< 0 >(std::move(*done));
}

// Retry a control call while it keeps failing (the cluster may still be forming or reconfiguring), up to
// a bounded number of attempts. The error surface no longer exposes the specific transient raft codes, so
// we simply re-issue on any failure.
template < typename MakeTask >
static int retry_until_ok(MakeTask&& make_task) {
    nuraft_mesg::null_result result =
        std::unexpected(std::make_error_condition(std::errc::resource_unavailable_try_again));
    for (int attempt = 0; !result && attempt < 25; ++attempt) {
        if (attempt) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }
        result = sync_get(make_task());
    }
    return result ? 0 : -1;
}

struct example_factory : public nuraft_mesg::group_factory {
    example_factory(int const threads, nuraft_mesg::group_id_t const& name) :
            nuraft_mesg::group_factory::group_factory(threads, name, nullptr) {}

    std::string lookup_endpoint(nuraft_mesg::peer_id_t const& client) override {
        auto id_str = to_string(client);
        for (auto i = 0u; i < 5; ++i) {
            if (uuids[i] == id_str) { return fmt::format(FMT_STRING("127.0.0.1:{}"), 9000 + i); }
        }
        RELEASE_ASSERT(false, "Missing Peer: {}", client);
        return std::string();
    }
};

int send_message(uint32_t leader_id, nuraft_mesg::group_id_t const& group_id, std::string const& message) {
    auto g_factory = std::make_shared< example_factory >(2, group_id);
    auto factory = std::make_shared< mesg_factory >(g_factory, group_id, "test_package");
    auto const dest_cfg = srv_config(leader_id, uuids[leader_id]);

    auto buf = buffer::alloc(message.length() + 1);
    buf->put(message.c_str());
    buf->pos(0);

    int ret = retry_until_ok([&] { return factory->append_entry(buf, dest_cfg); });
    sisl::GrpcAsyncClientWorker::shutdown_all();
    return ret;
}

int add_new_server(uint32_t leader_id, uint32_t srv_id, nuraft_mesg::group_id_t const& group_id) {
    auto g_factory = std::make_shared< example_factory >(2, group_id);
    auto factory = std::make_shared< mesg_factory >(g_factory, group_id, "test_package");
    auto const dest_cfg = srv_config(leader_id, uuids[leader_id]);

    auto const srv_addr = boost::uuids::string_generator()(uuids[srv_id]);
    int ret = retry_until_ok([&] { return factory->add_server(srv_id, srv_addr, dest_cfg); });
    sisl::GrpcAsyncClientWorker::shutdown_all();
    return ret;
}

int remove_server(uint32_t leader_id, nuraft_mesg::group_id_t const& group_id, uint32_t srv_id) {
    auto g_factory = std::make_shared< example_factory >(2, group_id);
    auto factory = std::make_shared< mesg_factory >(g_factory, group_id, "test_package");
    auto const dest_cfg = srv_config(leader_id, uuids[leader_id]);

    int ret = retry_until_ok([&] { return factory->rem_server(srv_id, dest_cfg); });
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

    auto guid_str = guids[SISL_OPTIONS["group"].as< uint32_t >()];
    auto gid = boost::uuids::uuid();
    try {
        gid = boost::uuids::string_generator()(guid_str);
    } catch (std::runtime_error const&) {
        LOGCRITICAL("Invalid uuid: {}", guid_str);
        return -1;
    }
    auto const server_id = SISL_OPTIONS["server"].as< uint32_t >();

    if (SISL_OPTIONS.count("echo")) {
        return send_message(server_id, gid, SISL_OPTIONS["echo"].as< std::string >());
    } else if (SISL_OPTIONS.count("add")) {
        return add_new_server(server_id, SISL_OPTIONS["add"].as< uint32_t >(), gid);
    } else if (SISL_OPTIONS.count("remove")) {
        return remove_server(server_id, gid, SISL_OPTIONS["remove"].as< uint32_t >());
    } else {
        std::cout << SISL_PARSER.help({}) << std::endl;
    }
    return 0;
}
