#include <cassert>
#include <csignal>

#include <boost/uuid/string_generator.hpp>
#include <nuraft_mesg/nuraft_mesg.hpp>
#include <sisl/grpc/rpc_client.hpp>
#include <sisl/grpc/rpc_server.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/utility/thread_buffer.hpp>

#include "example_state_manager.h"
#include "uuids.h"

SISL_OPTION_GROUP(server,
                  (server_id, "", "server_id", "Servers ID (0-9)", cxxopts::value< uint32_t >()->default_value("0"),
                   ""),
                  (start_group, "", "create", "Group to create", cxxopts::value< uint32_t >(), ""))

SISL_OPTIONS_ENABLE(logging, server, nuraft_mesg)

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

class Application : public nuraft_mesg::MessagingApplication, public std::enable_shared_from_this< Application > {
public:
    uint32_t port_;
    nuraft_mesg::peer_id_t id_;
    std::shared_ptr< nuraft_mesg::Manager > manager_;

    Application(nuraft_mesg::peer_id_t const& name, uint32_t port) : port_(port) { id_ = name; }
    ~Application() override = default;

    std::string lookup_peer(nuraft_mesg::peer_id_t const& peer) override {
        // Provide a method for the service layer to lookup an IPv4:port address
        // from a uuid; however the process wants to do that.
        auto id_str = to_string(peer);
        for (auto i = 0u; i < 5; ++i) {
            if (uuids[i] == id_str) { return fmt::format(FMT_STRING("127.0.0.1:{}"), 9000 + i); }
        }
        RELEASE_ASSERT(false, "Missing Peer: {}", peer);
        return std::string();
    }

    std::shared_ptr< nuraft_mesg::mesg_state_mgr > create_state_mgr(int32_t const srv_id,
                                                                    nuraft_mesg::group_id_t const& group_id) override {
        return std::static_pointer_cast< nuraft_mesg::mesg_state_mgr >(
            std::make_shared< simple_state_mgr >(srv_id, id_, group_id));
    }

    void start() {
        auto params = nuraft_mesg::Manager::Params();
        params.server_uuid_ = id_;
        params.mesg_port_ = port_;
        params.default_group_type_ = "test_package";
        manager_ = init_messaging(params, weak_from_this(), false);
        auto r_params = nuraft::raft_params()
                            .with_election_timeout_lower(elect_to_low)
                            .with_election_timeout_upper(elect_to_high)
                            .with_hb_interval(heartbeat_period)
                            .with_max_append_size(10)
                            .with_rpc_failure_backoff(rpc_backoff)
                            .with_auto_forwarding(true)
                            .with_snapshot_enabled(0);
        manager_->register_mgr_type(params.default_group_type_, r_params);
    }
};

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, logging, server, nuraft_mesg);

    // The offset_id is just a simple way to refer to the uuids
    // defined in uuids.h from the CLI without having to iterate
    // and store multiple maps in the code and test script.
    auto const offset_id = SISL_OPTIONS["server_id"].as< uint32_t >();
    auto const server_uuid = boost::uuids::string_generator()(uuids[offset_id]);

    // Can start using LOG from this point onward.
    sisl::logging::SetLogger(fmt::format(FMT_STRING("server_{}"), offset_id));
    spdlog::set_pattern("[%D %T] [%^%l%$] [%n] [%t] %v");

    signal(SIGINT, handle);
    signal(SIGTERM, handle);

    auto const server_port = 9000 + offset_id;
    LOGINFO("Server starting as: [{}], port: [{}]", server_uuid, server_port);

    auto app = std::make_shared< Application >(server_uuid, server_port);
    app->start();

    {
        auto lck = std::lock_guard< std::mutex >(k_stop_cv_lock);
        k_stop = false;
    }

    // Create a new group with ourself as the only member
    if (0 < SISL_OPTIONS.count("create")) {
        auto gid = boost::uuids::string_generator()(guids[SISL_OPTIONS["create"].as< uint32_t >()]);
        app->manager_->create_group(gid, "test_package");
    }

    // Just prevent main() from exiting, require a SIGNAL
    //{
    //    std::unique_lock< std::mutex > ulock(k_stop_cv_lock);
    //    k_stop_cv.wait(ulock, []() { return k_stop; });
    //}
    LOGWARN("Stopping Service!");
    return 0;
}
