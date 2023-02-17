#pragma once

#include <unordered_map>

namespace sisl {
struct io_blob;
}
namespace nuraft_mesg {

using data_service_request_handler_t = std::function< void(sisl::io_blob const& incoming_buf, void* rpc_data) >;

class data_service {

public:
    data_service() = default;
    virtual ~data_service() = default;

    // start the data service channel
    virtual void associate() = 0;

    // register a new rpc
    virtual bool bind(std::string const& request_name, std::string const& group_id,
                      data_service_request_handler_t const& request_cb) = 0;

    // register all the existing rpcs
    virtual void bind() = 0;
};

} // namespace nuraft_mesg
