#include <cassert>
#include <csignal>

#include <nuraft_mesg/messaging.hpp>
#include <sisl/grpc/rpc_client.hpp>
#include <sisl/grpc/rpc_server.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/utility/thread_buffer.hpp>

#include "example_state_manager.h"
#include "uuids.h"

SISL_OPTION_GROUP(server, (server_id, "", "server_id", "Servers ID (0-9)", cxxopts::value< uint32_t >(), ""),
                  (start_group, "", "create", "Group Name to create initialy", cxxopts::value< std::string >(), ""))

SISL_OPTIONS_ENABLE(logging, server, nuraft_mesg)
SISL_LOGGING_INIT(nuraft, nuraft_mesg, grpc_server, flip)

constexpr auto rpc_backoff = 50;
constexpr auto heartbeat_period = 100;
constexpr auto elect_to_low = heartbeat_period * 2;
constexpr auto elect_to_high = elect_to_low * 2;

static bool k_stop;
static std::condition_variable k_stop_cv;
static std::mutex k_stop_cv_lock;

void handle(int signal) {
    switch (signal) {
    case SIGINT:
        [[fallthrough]];
    case SIGTERM: {
        LOGWARN("SIGNAL: {}", strsignal(signal));
        {
            auto lck = std::lock_guard< std::mutex >(k_stop_cv_lock);
            k_stop = true;
        }
        k_stop_cv.notify_all();
    } break;
        ;
    default:
        LOGERROR("Unhandled SIGNAL: {}", strsignal(signal));
        break;
    }
}

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, logging, server, nuraft_mesg);

    // The offset_id is just a simple way to refer to the uuids
    // defined in uuids.h from the CLI without having to iterate
    // and store multiple maps in the code and test script.
    auto const offset_id = SISL_OPTIONS["server_id"].as< uint32_t >();
    auto const server_uuid = uuids[offset_id];

    // Can start using LOG from this point onward.
    sisl::logging::SetLogger(fmt::format(FMT_STRING("server_{}"), offset_id));
    spdlog::set_pattern("[%D %T] [%^%l%$] [%n] [%t] %v");

    signal(SIGINT, handle);
    signal(SIGTERM, handle);

    auto const server_port = 9000 + offset_id;
    LOGINFO("Server starting as: [{}], port: [{}]", server_uuid, server_port);

    // Provide a method for the service layer to lookup an IPv4:port address
    // from a uuid; however the process wants to do that.
    auto messaging_params =
        nuraft_mesg::consensus_component::params{server_uuid, server_port,
                                                    [](std::string const& client) -> std::string {
                                                        for (auto i = 0u; i < 5; ++i) {
                                                            if (uuids[i] == client) {
                                                                return fmt::format(FMT_STRING("127.0.0.1:{}"),
                                                                                   9000 + i);
                                                            }
                                                        }
                                                        return client;
                                                    },
                                                    "none"};

    // Intitialize the messaging layer.
    auto messaging = nuraft_mesg::service();

    // RAFT server parameters
    nuraft::raft_params r_params;
    r_params.with_election_timeout_lower(elect_to_low)
        .with_election_timeout_upper(elect_to_high)
        .with_hb_interval(heartbeat_period)
        .with_max_append_size(10)
        .with_rpc_failure_backoff(rpc_backoff)
        .with_auto_forwarding(true)
        .with_snapshot_enabled(0);
    // Each group has a type so we can attach different state_machines upon Join request.
    // This callback should provide a mechanism to return a new state_manager.
    auto group_type_params = nuraft_mesg::consensus_component::register_params{
        r_params,
        [server_uuid](int32_t const srv_id,
                      std::string const& group_id) -> std::shared_ptr< nuraft_mesg::mesg_state_mgr > {
            return std::make_shared< simple_state_mgr >(srv_id, server_uuid, group_id);
        }};
    messaging.register_mgr_type("test_package", group_type_params);

    // This will start the RPC service and begin listening for incomming JOIN groups request.
    // You can also call create_group and join_group following this operation.
    messaging.start(messaging_params);

    {
        auto lck = std::lock_guard< std::mutex >(k_stop_cv_lock);
        k_stop = false;
    }

    // Create a new group with ourself as the only member
    if (0 < SISL_OPTIONS.count("create")) {
        messaging.create_group(SISL_OPTIONS["create"].as< std::string >(), "test_package");
    }

    // Just prevent main() from exiting, require a SIGNAL
    {
        std::unique_lock< std::mutex > ulock(k_stop_cv_lock);
        k_stop_cv.wait(ulock, []() { return k_stop; });
    }
    LOGERROR("Stopping Service!");
    return 0;
}
