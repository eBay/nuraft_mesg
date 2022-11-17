///
// Copyright 2018 (c) eBay Corporation
//

#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>

#include <nuraft_grpc/grpc_factory.hpp>
#include <sisl/logging/logging.h>
#include <sisl/metrics/metrics.hpp>

namespace sds::messaging {

using group_name_t = std::string;
using group_type_t = std::string;

class mesg_client;

template < typename T >
using shared = std::shared_ptr< T >;

class group_factory : public nuraft_grpc::grpc_factory {
    std::shared_ptr< sisl::TrfClient > m_trf_client;
    static std::string m_ssl_cert;

public:
    group_factory(int const cli_thread_count, std::string const& name, shared< sisl::TrfClient > const trf_client,
                  std::string const& ssl_cert = "");

    using nuraft_grpc::grpc_factory::create_client;

    std::error_condition create_client(const std::string& client, nuraft::ptr< nuraft::rpc_client >&) override;

    std::error_condition reinit_client(std::string const& client,
                                       nuraft_grpc::shared< nuraft::rpc_client >& raft_client) override;

    virtual std::string lookupEndpoint(std::string const& client) = 0;
};

class mesg_factory final : public nuraft_grpc::grpc_factory {
    shared< group_factory > _group_factory;
    group_name_t const _group_name;
    group_type_t const _group_type;
    shared< sisl::MetricsGroupWrapper > _metrics;

public:
    mesg_factory(shared< group_factory > g_factory, group_name_t const& grp_id, group_type_t const& grp_type,
                 shared< sisl::MetricsGroupWrapper > metrics = nullptr) :
            nuraft_grpc::grpc_factory(0, grp_id),
            _group_factory(g_factory),
            _group_name(grp_id),
            _group_type(grp_type),
            _metrics(metrics) {}

    group_name_t group_name() const { return _group_name; }

    std::error_condition create_client(const std::string& client, nuraft::ptr< nuraft::rpc_client >& rpc_ptr) override;

    std::error_condition reinit_client(const std::string& client,
                                       nuraft_grpc::shared< nuraft::rpc_client >& raft_client) override;
};

} // namespace sds::messaging
