#include "test_fixture.ipp"

static nuraft::ptr< nuraft::buffer > create_message(nlohmann::json const& j_obj) {
    auto v_msgpack = nlohmann::json::to_msgpack(j_obj);
    auto buf = nuraft::buffer::alloc(v_msgpack.size() + sizeof(int32_t));
    buf->put(&v_msgpack[0], v_msgpack.size());
    buf->pos(0);
    return buf;
}

class MessagingFixture : public MessagingFixtureBase {
protected:
    void SetUp() override {
        MessagingFixtureBase::SetUp();
        start();
    }
};

// Every MessagingFixture setups a whole new default RAFT group, we put all the tests here
// that operate on this group so we don't have to restart it over and over.
TEST_F(MessagingFixture, BasicTests) {
    auto const bogus_uuid = boost::uuids::random_generator()();
    auto buf = create_message(nlohmann::json{
        {"op_type", 2},
    });
    // Basic resiliency test (append_entries)
    EXPECT_TRUE(app_1_->instance_->append_entries(group_id_, {buf}).get());

    // Simulate a Member crash
    auto our_id = app_3_->id_;
    app_3_.reset();

    // Commit message
    buf = create_message(nlohmann::json{
        {"op_type", 2},
    });
    auto factory = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");
    auto const dest_cfg_1 = nuraft::srv_config(to_server_id(app_1_->id_), to_string(app_1_->id_));
    auto const dest_cfg_2 = nuraft::srv_config(to_server_id(app_2_->id_), to_string(app_2_->id_));
    EXPECT_TRUE(factory->append_entry(buf, dest_cfg_1).get());
    EXPECT_TRUE(factory->append_entry(buf, dest_cfg_2).get());

    app_3_ = std::make_shared< TestApplication >("sm3", ports[2]);
    app_3_->set_id(our_id);
    app_3_->map_peers(lookup_map);
    app_3_->start();
    app_3_->instance_->join_group(
        group_id_, "test_type",
        std::make_shared< test_state_mgr >(nuraft_mesg::to_server_id(our_id), our_id, group_id_));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_FALSE(app_3_->instance_->become_leader(bogus_uuid).get());
    EXPECT_TRUE(app_3_->instance_->become_leader(group_id_).get());
    EXPECT_TRUE(app_3_->instance_->append_entries(group_id_, {buf}).get());

    // Test sending a message for a group the messaging service is not aware of.
    EXPECT_FALSE(app_1_->instance_->add_member(bogus_uuid, bogus_uuid).get());

    // Add a 4th Member to the Group
    std::vector< std::shared_ptr< nuraft::srv_config > > srv_list;
    app_3_->instance_->get_srv_config_all(group_id_, srv_list);
    EXPECT_EQ(srv_list.size(), 3u);

    // Ensure lookup_works for the new member
    get_random_ports(1u);
    auto app_4 = std::make_shared< TestApplication >("sm4", ports[3]);
    lookup_map.emplace(app_4->id_, fmt::format("127.0.0.1:{}", ports[3]));
    app_1_->map_peers(lookup_map);
    app_2_->map_peers(lookup_map);
    app_3_->map_peers(lookup_map);
    app_4->map_peers(lookup_map);
    app_4->start();

    // Add the member and wait
    EXPECT_TRUE(app_3_->instance_->add_member(group_id_, app_4->id_).get());
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // New member should appear in config now
    srv_list.clear();
    app_3_->instance_->get_srv_config_all(group_id_, srv_list);
    EXPECT_EQ(srv_list.size(), 4u);

    // Remove a member now
    EXPECT_TRUE(app_3_->instance_->rem_member(group_id_, app_1_->id_).get());

    // Unknown Group Tests
    app_1_->instance_->leave_group(bogus_uuid);

    EXPECT_FALSE(app_1_->instance_->append_entries(bogus_uuid, {buf}).get());

    // Expect failure trying to remove unknown member
    auto const dest_cfg = nuraft::srv_config(to_server_id(app_1_->id_), to_string(app_1_->id_));
    EXPECT_FALSE(factory->rem_server(1000, dest_cfg).get());

    // Expect failure trying to remove unknown group
    EXPECT_FALSE(app_2_->instance_->rem_member(bogus_uuid, app_3_->id_).get());

    // Needed since app_4 is not part of TearDown
    app_4->instance_->leave_group(group_id_);
}
