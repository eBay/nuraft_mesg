/**
 * Copyright eBay Corporation 2018
 */

#include <nuraft_grpc/grpc_client.hpp>

#include "factory.h"
#include "service.h"
#include "messaging_service.grpc.pb.h"

SDS_LOGGING_DECL(sds_msg)

namespace sds::messaging {

class messaging_client : public sds::grpc_client< Messaging > {
public:
    using sds::grpc_client< Messaging >::grpc_client;
    ~messaging_client() override = default;

    using sds::grpc_base_client::send;

    void send(RaftGroupMsg const& message, handle_resp complete) {
        auto group_compl = [complete](RaftGroupMsg& response, ::grpc::Status& status) mutable -> void {
            complete(*response.mutable_message(), status);
        };

        _stub->call_unary< RaftGroupMsg, RaftGroupMsg >(message, &Messaging::StubInterface::AsyncRaftStep, group_compl);
    }

protected:
    void send(sds::RaftMessage const&, handle_resp) override { throw std::runtime_error("Bad call!"); }
};

class group_client : public sds::grpc_base_client {
    shared< messaging_client > _client;
    group_name_t const         _group_name;
    shared< group_metrics >    _metrics;

public:
    group_client(shared< messaging_client > client, group_name_t const& grp_name,
                 shared< sisl::MetricsGroupWrapper > metrics) :
            sds::grpc_base_client(),
            _client(client),
            _group_name(grp_name),
            _metrics(std::static_pointer_cast< group_metrics >(metrics)) {}

    ~group_client() override = default;

    shared< messaging_client > realClient() { return _client; }
    void setClient(shared< messaging_client > new_client) { _client = new_client; }

    void send(sds::RaftMessage const& message, handle_resp complete) override {
        RaftGroupMsg group_msg;

        LOGTRACEMOD(sds_msg, "Sending [{}] from: [{}] to: [{}] Group: [{}]",
                    nuraft::msg_type_to_string(nuraft::msg_type(message.base().type())), message.base().src(),
                    message.base().dest(), _group_name);
        if (_metrics) { COUNTER_INCREMENT(*_metrics, group_sends, 1); }
        group_msg.set_group_name(_group_name);
        group_msg.mutable_message()->CopyFrom(message);
        _client->send(group_msg, complete);
    }
};

std::error_condition mesg_factory::create_client(const std::string&                 client,
                                                 nuraft::ptr< nuraft::rpc_client >& raft_client) {
    // Re-direct this call to a global factory so we can re-use clients to the same endpoints
    LOGDEBUGMOD(sds_msg, "Creating client to {}", client);
    auto m_client = std::dynamic_pointer_cast< messaging_client >(_group_factory->create_client(client));
    raft_client = std::make_shared< group_client >(m_client, _group_name, _metrics);
    return (!raft_client) ? std::make_error_condition(std::errc::invalid_argument) : std::error_condition();
}

std::error_condition mesg_factory::reinit_client(const std::string&                 client,
                                                 sds::shared< nuraft::rpc_client >& raft_client) {
    LOGDEBUGMOD(sds_msg, "Re-init client to {}", client);
    auto g_client = std::dynamic_pointer_cast< group_client >(raft_client);
    auto new_raft_client = std::static_pointer_cast< nuraft::rpc_client >(g_client->realClient());
    if (auto err = _group_factory->reinit_client(client, new_raft_client); err) {
        return err;
    }
    g_client->setClient(std::dynamic_pointer_cast< messaging_client >(new_raft_client));
    return std::error_condition();
}

std::error_condition group_factory::create_client(const std::string&                 client,
                                                  nuraft::ptr< nuraft::rpc_client >& raft_client) {
    LOGDEBUGMOD(sds_msg, "Creating client to {}", client);
    auto endpoint = lookupEndpoint(client);
    if (endpoint.empty()) { return std::make_error_condition(std::errc::invalid_argument); }

    LOGDEBUGMOD(sds_msg, "Creating client for [{}] @ [{}]", client, endpoint);
    raft_client = sds::grpc::GrpcAsyncClient::make< messaging_client >(workerName(), endpoint);
    return (!raft_client) ? std::make_error_condition(std::errc::connection_aborted) : std::error_condition();
}

std::error_condition group_factory::reinit_client(const std::string&                 client,
                                                  sds::shared< nuraft::rpc_client >& raft_client) {
    LOGDEBUGMOD(sds_msg, "Re-init client to {}", client);
    assert(raft_client);
    auto const connection_ready = std::dynamic_pointer_cast< messaging_client >(raft_client)->is_connection_ready();
    if (!connection_ready) {
        return create_client(client, raft_client);
    }
    return std::error_condition();
}

} // namespace sds::messaging
