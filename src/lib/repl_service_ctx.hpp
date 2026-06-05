#pragma once

#include <optional>
#include <string>

#include "nuraft_mesg/mesg_state_mgr.hpp"
#include "nuraft_mesg/common.hpp"

namespace nuraft_mesg {

class grpc_server;
class mesg_factory;

class repl_service_ctx_grpc : public repl_service_ctx {
public:
    repl_service_ctx_grpc(grpc_server* server, std::shared_ptr< mesg_factory > const& cli_factory);
    ~repl_service_ctx_grpc() override = default;
    std::shared_ptr< mesg_factory > m_mesg_factory;

    null_async_task data_service_request_unidirectional(destination_t dest, std::string request_name,
                                                      io_blob_list_t cli_buf) override;
    async_task< sisl::GenericClientResponse >
    data_service_request_bidirectional(destination_t dest, std::string request_name,
                                       io_blob_list_t cli_buf) override;
    void send_data_service_response(io_blob_list_t const& outgoing_buf,
                                    boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) override;

private:
    resolved_dest resolve_dest(destination_t const& dest) const;
    const std::string id_to_str(int32_t const id) const;
};

} // namespace nuraft_mesg
