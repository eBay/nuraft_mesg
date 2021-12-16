///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//

#include <cassert>

#include <libjungle/jungle.h>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <nuraft_grpc/simple_server.hpp>

#include "example_factory.h"
#include "example_logger.h"
#include "example_state_machine.h"
#include "example_state_manager.h"

using namespace nuraft_grpc;

SISL_OPTION_GROUP(server, (server_id, "", "server_id", "Servers ID", cxxopts::value< uint32_t >(), ""))

SISL_OPTIONS_ENABLE(logging, server)
SISL_LOGGING_INIT(nuraft, nublox_logstore, HOMESTORE_LOG_MODS, grpc_server)

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, logging, server);
    auto server_id = SISL_OPTIONS["server_id"].as< uint32_t >();
    auto server_address = format(FMT_STRING("0.0.0.0:{}"), 9000 + server_id);

    // Can start using LOG from this point onward.
    sisl::logging::SetLogger(format(FMT_STRING("server_{}"), server_id));
    spdlog::set_pattern("[%D %T] [%^%l%$] [%n] [%t] %v");

    jungle::GlobalConfig g_config;
    g_config.numFlusherThreads = 0;
    g_config.numCompactorThreads = 0;
    g_config.logFileReclaimerSleep_sec = 60;
    jungle::init(g_config);

    // State manager (RAFT log store, config).
    ptr< state_mgr > smgr = std::make_shared< simple_state_mgr >(server_id);

    // State machine.
    ptr< state_machine > smachine = std::make_shared< echo_state_machine >();

    // Parameters.
    raft_params params;
    params.with_election_timeout_lower(400)
        .with_election_timeout_upper(800)
        .with_hb_interval(200)
        .with_max_append_size(50)
        .with_rpc_failure_backoff(100);

    ptr< logger > l = std::make_shared< sds_logger >();
    ptr< rpc_client_factory > rpc_cli_factory = std::make_shared< example_factory >(2, server_address);
    ptr< asio_service > asio_svc_ = std::make_shared< asio_service >();
    ptr< delayed_task_scheduler > scheduler = std::static_pointer_cast< delayed_task_scheduler >(asio_svc_);
    ptr< rpc_listener > listener;

    // Run server.
    {
        auto server = std::make_shared< raft_server >(
            new context(smgr, smachine, listener, l, rpc_cli_factory, scheduler, params));
        auto grpc_svc_ = std::make_unique< nuraft_grpc::simple_server >(server);

        auto grpc_server =
            std::unique_ptr< grpc_helper::GrpcServer >(grpc_helper::GrpcServer::make(server_address, 2, "", ""));
        grpc_svc_->associate(grpc_server.get());
        grpc_server->run();
        grpc_svc_->bind(grpc_server.get());

        std::condition_variable stop_cv;
        std::mutex stop_cv_lock;
        {
            std::unique_lock< std::mutex > ulock(stop_cv_lock);
            stop_cv.wait(ulock);
            grpc_server->shutdown();
        }
    }
    grpc_helper::GrpcAsyncClientWorker::shutdown_all();
    return 0;
}
