///
// Copyright 2018 (c) eBay Corporation
//

#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>

#include <nuraft_grpc/grpc_factory.hpp>
#include <sds_logging/logging.h>
#include <metrics/metrics.hpp>

namespace sds::messaging {

using group_name_t = std::string;

class mesg_client;

template < typename T >
using shared = std::shared_ptr< T >;

class group_factory : public sds::grpc_factory {
public:
    using sds::grpc_factory::grpc_factory;

    using sds::grpc_factory::create_client;

    std::error_condition create_client(const std::string& client, nuraft::ptr< nuraft::rpc_client >&) override;

    std::error_condition reinit_client(sds::shared< nuraft::rpc_client >& raft_client) override;

    virtual std::string lookupEndpoint(std::string const& client) = 0;
};

class mesg_factory final : public sds::grpc_factory {
    shared< group_factory >             _group_factory;
    group_name_t const                  _group_name;
    shared< sisl::MetricsGroupWrapper > _metrics;

public:
    mesg_factory(shared< group_factory > g_factory, group_name_t const& grp_id,
                 shared< sisl::MetricsGroupWrapper > metrics = nullptr) :
            sds::grpc_factory(0, grp_id),
            _group_factory(g_factory),
            _group_name(grp_id),
            _metrics(metrics) {}

    group_name_t group_name() const { return _group_name; }

    std::error_condition create_client(const std::string& client, nuraft::ptr< nuraft::rpc_client >& rpc_ptr) override;

    std::error_condition reinit_client(sds::shared< nuraft::rpc_client >& raft_client) override;
};

} // namespace sds::messaging
