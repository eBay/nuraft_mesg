// Minimal example that exercises the nuraft_mesg public API surface.
// For a complete working server, see example_server.cpp.

#include <memory>
#include <string>

#include <nuraft_mesg/nuraft_mesg.hpp>
#include <nuraft_mesg/mesg_state_mgr.hpp>
#include <nuraft_mesg/common.hpp>

struct example_state_mgr : public nuraft_mesg::mesg_state_mgr {
    example_state_mgr(int32_t srv_id, nuraft_mesg::peer_id_t const&, nuraft_mesg::group_id_t const&) :
            _srv_id(srv_id) {}

    nuraft::ptr< nuraft::cluster_config > load_config() override { return nullptr; }
    void save_config(const nuraft::cluster_config&) override {}
    void save_state(const nuraft::srv_state&) override {}
    nuraft::ptr< nuraft::srv_state > read_state() override { return nullptr; }
    nuraft::ptr< nuraft::log_store > load_log_store() override { return nullptr; }
    int32_t server_id() override { return _srv_id; }
    void system_exit(int) override {}

    uint32_t get_logstore_id() const override { return 0; }
    std::shared_ptr< nuraft::state_machine > get_state_machine() override { return nullptr; }
    void permanent_destroy() override {}
    void leave() override {}

private:
    int32_t const _srv_id;
};

struct example_app : public nuraft_mesg::messaging_application {
    std::string lookup_peer(nuraft_mesg::peer_id_t const&) override { return {}; }
    std::shared_ptr< nuraft_mesg::mesg_state_mgr > create_state_mgr(int32_t srv_id,
                                                                     nuraft_mesg::group_id_t const& gid) override {
        return std::make_shared< example_state_mgr >(srv_id, nuraft_mesg::peer_id_t{}, gid);
    }
};

int main() { return 0; }
