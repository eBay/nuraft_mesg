///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//

#include <cassert>

#include <raft_core_grpc/simple_server.hpp>
#include <sds_logging/logging.h>

#include "example_factory.h"
#include "example_logger.h"
#include "example_state_machine.h"
#include "example_state_manager.h"

using namespace cornerstone;

SDS_OPTION_GROUP(server, (server_id, "", "server_id", "Servers ID (0-9)", cxxopts::value<uint32_t>(), ""))

SDS_OPTIONS_ENABLE(logging, server)
SDS_LOGGING_INIT(raft_core)

int main(int argc, char** argv) {
    SDS_OPTIONS_LOAD(argc, argv, logging, server);
    auto server_id = SDS_OPTIONS["server_id"].as<uint32_t>();
    auto server_address = format(FMT_STRING("0.0.0.0:900{}"), server_id);

    // Can start using LOG from this point onward.
    sds_logging::SetLogger(format(FMT_STRING("server_{}"), server_id));
    spdlog::set_pattern("[%D %T] [%^%l%$] [%n] [%t] %v");

    if (0 <= server_id && 10 > server_id) {
        // State manager (RAFT log store, config).
        ptr<state_mgr> smgr = std::make_shared<simple_state_mgr>(server_id);

        // State machine.
        ptr<state_machine> smachine = std::make_shared<echo_state_machine>();

        // Parameters.
        raft_params* params(new raft_params());
        (*params)
            .with_election_timeout_lower(200)
            .with_election_timeout_upper(400)
            .with_hb_interval(100)
            .with_max_append_size(100)
            .with_rpc_failure_backoff(50);

        ptr<logger> l = std::make_shared<sds_logger>();
        ptr<rpc_client_factory> rpc_cli_factory = std::make_shared<example_factory>(server_id);
        ptr<asio_service> asio_svc_ = std::make_shared<asio_service>();
        ptr<delayed_task_scheduler> scheduler = std::static_pointer_cast<delayed_task_scheduler>(asio_svc_);
        ptr<rpc_listener> listener;

        // Run server.
        auto server = std::make_unique<raft_server>(new context(smgr,
                                                                smachine,
                                                                listener,
                                                                l,
                                                                rpc_cli_factory,
                                                                scheduler,
                                                                params));
        auto grpc_svc_ = std::make_unique<raft_core::simple_server>(std::move(server));

        auto grpc_server = sds::grpc::GrpcServer::make(server_address, 2, "", "");
        grpc_svc_->associate(grpc_server);
        grpc_server->run();
        grpc_svc_->bind(grpc_server);

        std::condition_variable stop_cv;
        std::mutex stop_cv_lock;
        {
            std::unique_lock<std::mutex> ulock(stop_cv_lock);
            stop_cv.wait(ulock);
            grpc_server->shutdown();
        }
        delete grpc_server;
    } else {
        LOGERROR("Server ID must be between 0 and 9");
    }
    return 0;
}
