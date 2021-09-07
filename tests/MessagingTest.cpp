#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <sds_grpc/client.h>
#include <sds_logging/logging.h>
#include <sds_options/options.h>
#include <nlohmann/json.hpp>

#include <utility/thread_buffer.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libnuraft/cluster_config.hxx"
#include "libnuraft/state_machine.hxx"
#include "consensus_impl.h"

#include "test_state_manager.h"

SDS_LOGGING_INIT(nuraft, sds_msg)
SDS_OPTIONS_ENABLE(logging, messaging)

THREAD_BUFFER_INIT;

namespace sds::messaging {

extern nuraft::ptr< nuraft::cluster_config > fromClusterConfig(nlohmann::json const& cluster_config);

static nuraft::ptr< nuraft::buffer > create_message(nlohmann::json const& j_obj) {
    auto j_str = j_obj.dump();
    auto v_msgpack = nlohmann::json::to_msgpack(j_obj);
    auto buf = nuraft::buffer::alloc(v_msgpack.size() + sizeof(int32_t));
    buf->put(&v_msgpack[0], v_msgpack.size());
    buf->pos(0);
    return buf;
}

} // namespace sds::messaging

using namespace sds::messaging;
using testing::_;
using testing::Return;

class MessagingFixture : public ::testing::Test {
protected:
    std::unique_ptr< consensus_impl > instance_1;
    std::unique_ptr< consensus_impl > instance_2;
    std::unique_ptr< consensus_impl > instance_3;

    std::shared_ptr< test_state_mgr > sm_int_1;

    std::string id_1;
    std::string id_2;
    std::string id_3;

    void SetUp() override {
        id_1 = to_string(boost::uuids::random_generator()());
        id_2 = to_string(boost::uuids::random_generator()());
        id_3 = to_string(boost::uuids::random_generator()());

        instance_1 = std::make_unique< consensus_impl >();
        instance_2 = std::make_unique< consensus_impl >();
        instance_3 = std::make_unique< consensus_impl >();

        auto lookup_callback = [srv_1 = id_1, srv_2 = id_2, srv_3 = id_3](std::string const& id) -> std::string {
            if (id == srv_1)
                return "127.0.0.1:9001";
            else if (id == srv_2)
                return "127.0.0.1:9002";
            else if (id == srv_3)
                return "127.0.0.1:9003";
            else
                return std::string();
        };

        auto create_state_mgr = [this, srv_addr = id_1](int32_t const srv_id,
                                                  std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            sm_int_1 = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
            return std::static_pointer_cast< mesg_state_mgr >(sm_int_1);
        };

        auto params = consensus_component::params{id_1, 9001, lookup_callback, create_state_mgr};
        instance_1->start(params);

        params.server_uuid = id_2;
        params.mesg_port = 9002;
        params.create_state_mgr = [srv_addr = id_2](int32_t const srv_id,
                                                    std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            auto new_sm = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
            return std::static_pointer_cast< mesg_state_mgr >(new_sm);
        };
        instance_2->start(params);

        params.server_uuid = id_3;
        params.mesg_port = 9003;
        params.create_state_mgr = [srv_addr = id_3](int32_t const srv_id,
                                                    std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            auto new_sm = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
            return std::static_pointer_cast< mesg_state_mgr >(new_sm);
        };
        instance_3->start(params);

        instance_1->create_group("test_group");

        EXPECT_TRUE(instance_1->add_member("test_group", id_2));
        EXPECT_TRUE(instance_1->add_member("test_group", id_3));
    }

    void TearDown() override {
        instance_1.reset();
        instance_2.reset();
        instance_3.reset();
    }

public:
};

TEST_F(MessagingFixture, ClientRequest) {
    auto buf = sds::messaging::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_FALSE(instance_1->client_request("test_group", buf));

    instance_3->leave_group("test_group");
    instance_2->leave_group("test_group");
    instance_1->leave_group("test_group");
}
TEST_F(MessagingFixture, UnknownGroup) {
    EXPECT_FALSE(instance_1->add_member("unknown_group", to_string(boost::uuids::random_generator()())));

    instance_1->leave_group("unknown_group");

    auto buf = sds::messaging::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_TRUE(instance_1->client_request("unknown_group", buf));
}

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging, messaging)
    ::testing::InitGoogleTest(&argc, argv);
    sds_logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T.%f%z] [%^%l%$] [%t] %v");
    sds_logging::GetLogger()->flush_on(spdlog::level::level_enum::err);

    auto ret = RUN_ALL_TESTS();
    sds::grpc::GrpcAyncClientWorker::shutdown_all();

    return ret;
}
