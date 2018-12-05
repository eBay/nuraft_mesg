
#include <raft_core_grpc/grpc_client.hpp>

#include "factory.h"
#include "messaging_service.grpc.pb.h"

namespace sds::messaging {

class messaging_client :
    public raft_core::grpc_client<Messaging>
{
 public:
    messaging_client(std::string const& addr,
                     std::string const& target_domain,
                     std::string const& ssl_cert,
                     int32 n_threads) :
        raft_core::grpc_client<Messaging>(addr, target_domain, ssl_cert, n_threads)
    {}

    ~messaging_client() override = default;

    void send(raft_core::RaftMessage const &, handle_resp ) override {
        throw std::runtime_error("Bad call!");
    }

    void send(RaftGroupMsg const &message, handle_resp complete) {
        auto group_compl =
            [complete] (RaftGroupMsg& response, ::grpc::Status& status) mutable -> void
            { complete(*response.mutable_message(), status); };

        _stub->call_unary<RaftGroupMsg, RaftGroupMsg>(message,
                                                      &Messaging::StubInterface::AsyncRaftStep,
                                                      group_compl);
    }
};

class group_client :
    public raft_core::grpc_base_client
{
    shared<messaging_client> _client;
    group_name_t const _group_name;

 public:
    group_client(shared<messaging_client> client,
                 group_name_t const& grp_name) :
        raft_core::grpc_base_client(),
        _client(client),
        _group_name(grp_name)
    {}

    group_client(std::string const& addr,
                 std::string const& target_domain,
                 std::string const& ssl_cert,
                 int32 n_threads,
                 group_name_t const& grp_name) :
        raft_core::grpc_base_client(),
        _client(std::make_shared<messaging_client>(addr, target_domain, ssl_cert, n_threads)),
        _group_name(grp_name)
    { }

    ~group_client() override = default;

    shared<messaging_client> realClient() { return _client; }

    void send(raft_core::RaftMessage const &message, handle_resp complete) override {
        RaftGroupMsg group_msg;

        LOGTRACEMOD(sds_msg, "Sending [{}] from: [{}] to: [{}] Group: [{}]",
                message.base().type(),
                message.base().src(),
                message.base().dest(),
                _group_name);
        group_msg.set_group_name(_group_name);
        group_msg.mutable_message()->CopyFrom(message);
        _client->send(group_msg, complete);
    }
};


std::error_condition
grpc_factory::create_client(const std::string &client, cstn::ptr<cstn::rpc_client>& raft_client) {
    // Re-direct this call to a global factory so we can re-use clients to the same endpoints
    auto m_client = std::dynamic_pointer_cast<messaging_client>(_group_factory->create_client(client));
    raft_client = std::make_shared<group_client>(m_client, _group_name);
    return (!raft_client) ?
        std::make_error_condition(std::errc::invalid_argument) :
        std::error_condition();
}

std::error_condition
grpc_factory::reinit_client(raft_core::shared<cstn::rpc_client>& raft_client) {
    auto client = std::dynamic_pointer_cast<group_client>(raft_client);
    auto real_client = std::static_pointer_cast<cstn::rpc_client>(client->realClient());
    return _group_factory->reinit_client(real_client);
}

std::error_condition
group_factory::create_client(const std::string &client, cstn::ptr<cstn::rpc_client>& raft_client) {
    auto endpoint = lookupEndpoint(client);
    if (endpoint.empty()) {
        return std::make_error_condition(std::errc::invalid_argument);
    }

    LOGDEBUGMOD(sds_msg, "Creating client for [{}] @ [{}]", client, endpoint);
    raft_client = sds::grpc::GrpcAsyncClient::make<messaging_client>(endpoint, "", "", 2);
    return (!raft_client) ?
        std::make_error_condition(std::errc::connection_aborted) :
        std::error_condition();
}

std::error_condition
group_factory::reinit_client(raft_core::shared<cstn::rpc_client>& raft_client) {
    assert(raft_client);
    auto grpc_client = std::dynamic_pointer_cast<messaging_client>(raft_client);
    return (!grpc_client->init()) ?
        std::make_error_condition(std::errc::connection_aborted) :
        std::error_condition();
}

}
