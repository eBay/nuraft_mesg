#pragma once

#include <memory>

#include <libnuraft/nuraft.hxx>

#include "common.hpp"

namespace boost {
template < class T >
class intrusive_ptr;
} // namespace boost

namespace sisl {
class GenericRpcData;
}

namespace nuraft_mesg {

class mesg_factory;
class grpc_server;

class repl_service_ctx {
public:
    repl_service_ctx(grpc_server* server);
    virtual ~repl_service_ctx() = default;

    // we do not own this pointer. Use this only if the life cycle of the pointer is well known
    grpc_server* m_server;
    bool is_raft_leader() const;

    // data service api client calls
    virtual NullAsyncResult data_service_request_unidirectional(destination_t const& dest,
                                                                std::string const& request_name,
                                                                io_blob_list_t const& cli_buf) = 0;
    virtual IoBlobAsyncResult data_service_request_bidirectional(destination_t const& dest,
                                                                 std::string const& request_name,
                                                                 io_blob_list_t const& cli_buf) = 0;

    // Will be removed after the above two APIs are implemented
    virtual IoBlobAsyncResult data_service_request(std::string const& request_name, io_blob_list_t const& cli_buf) = 0;

    // Send response to a data service request and finish the async call.
    virtual void send_data_service_response(io_blob_list_t const& outgoing_buf,
                                            boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) = 0;
};

class mesg_state_mgr : public nuraft::state_mgr {
public:
    using nuraft::state_mgr::state_mgr;
    virtual ~mesg_state_mgr() = default;
    void make_repl_ctx(grpc_server* server, std::shared_ptr< mesg_factory >& cli_factory);

    virtual void become_ready() {}
    virtual uint32_t get_logstore_id() const = 0;
    virtual std::shared_ptr< nuraft::state_machine > get_state_machine() = 0;
    virtual void permanent_destroy() = 0;
    virtual void leave() = 0;

protected:
    std::unique_ptr< repl_service_ctx > m_repl_svc_ctx;
};

} // namespace nuraft_mesg
