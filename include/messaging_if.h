///
// Copyright 2018, eBay Corporation
///

#pragma once

#include <list>
#include <memory>
#include <string>
#include <system_error>

#include <libnuraft/nuraft.hxx>

namespace sds::messaging {

class mesg_state_mgr : public nuraft::state_mgr {
public:
    using nuraft::state_mgr::state_mgr;

    virtual void become_ready() {}
    virtual uint32_t get_logstore_id() const = 0;
    virtual std::shared_ptr< nuraft::state_machine > get_state_machine() = 0;
    virtual void permanent_destroy() = 0;
};

class consensus_component {
public:
    using lookup_peer_cb = std::function< std::string(std::string const&) >;
    using create_state_mgr_cb =
        std::function< std::shared_ptr< mesg_state_mgr >(int32_t const srv_id, std::string const& group_id) >;

    struct params {
        std::string server_uuid;
        uint32_t mesg_port;
        lookup_peer_cb lookup_peer;
        std::string default_group_type;
    };
    virtual ~consensus_component() = default;
    virtual void start(consensus_component::params& start_params) = 0;

    // Register a new state_mgr type
    struct register_params {
        create_state_mgr_cb create_state_mgr;
    };
    virtual void register_mgr_type(std::string const& group_type, register_params& params) = 0;

    virtual std::error_condition create_group(std::string const& group_id, std::string const& group_type) = 0;
    virtual std::error_condition join_group(std::string const& group_id, std::string const& group_type, std::shared_ptr< mesg_state_mgr > smgr) = 0;

    // Send a client request to the cluster
    virtual bool add_member(std::string const& group_id, std::string const& server_id) = 0;
    virtual bool rem_member(std::string const& group_id, std::string const& server_id) = 0;
    virtual void leave_group(std::string const& group_id) = 0;
    virtual bool request_leadership(std::string const& group_id) = 0;
    virtual std::error_condition client_request(std::string const& group_id,
                                                std::shared_ptr< nuraft::buffer >& buf) = 0;
    virtual void get_peers(std::string const& group_id, std::list< std::string >&) const = 0;
    virtual uint32_t logstore_id(std::string const& group_id) const = 0;
    virtual int32_t server_id() const = 0;
};

} // namespace access_mgr
