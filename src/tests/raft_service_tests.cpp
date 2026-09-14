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
        start(true);
    }
};

// Every MessagingFixture setups a whole new default RAFT group, we put all the tests here
// that operate on this group so we don't have to restart it over and over.
TEST_F(MessagingFixture, BasicTests) {
    auto const bogus_uuid = boost::uuids::random_generator()();
    auto buf = create_message(nlohmann::json{
        {"op_type", 2},
    });

    auto sm1 = app_1_->state_mgr_map_[group_id_];
    auto repl_ctx1 = sm1->get_repl_context();
    //app_1 is leader
    EXPECT_TRUE(repl_ctx1->is_raft_leader());

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
    app_3_->start(true);
    auto sm3 = std::make_shared< test_state_mgr >(nuraft_mesg::to_server_id(our_id), our_id, group_id_);
    app_3_->instance_->join_group(group_id_, "test_type", sm3);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_FALSE(app_3_->instance_->become_leader(bogus_uuid).get());
    EXPECT_TRUE(app_3_->instance_->become_leader(group_id_).get());
    // now app_3 is the leader via explicity reqeust
    {
        auto repl_ctx3 = sm3->get_repl_context();
        EXPECT_TRUE(repl_ctx3->is_raft_leader());
    }
    EXPECT_TRUE(app_3_->instance_->append_entries(group_id_, {buf}).get());

    // Test sending a message for a group the messaging service is not aware of.
    EXPECT_FALSE(app_1_->instance_->add_member(bogus_uuid, bogus_uuid).get());

    // Simulate app_3 crash again
    app_3_.reset();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    //now app_1 will be the leader as it has highest priority
    EXPECT_TRUE(repl_ctx1->is_raft_leader());

    //restart app_3
    app_3_ = std::make_shared< TestApplication >("sm3", ports[2]);
    app_3_->set_id(our_id);
    app_3_->map_peers(lookup_map);
    app_3_->start(true);
    sm3 = std::make_shared< test_state_mgr >(nuraft_mesg::to_server_id(our_id), our_id, group_id_);
    app_3_->instance_->join_group(
        group_id_, "test_type", sm3);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // leader shoud still on app_1
    EXPECT_TRUE(repl_ctx1->is_raft_leader());
    // app_3 take over leadership
    EXPECT_TRUE(app_3_->instance_->become_leader(group_id_).get());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // now app_3 is the leader via explicity reqeust
    {
        auto repl_ctx3 = sm3->get_repl_context();
        EXPECT_TRUE(repl_ctx3->is_raft_leader());
    }

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

// NuRaft peer::recreate_rpc() calls factory->create_client() again and relies on
// rpc_client::get_id() changing so delayed responses are treated as stale and
// skip bytes_in_flight_sub(). mesg_factory must mint a new outer wrapper on
// reinit even when the underlying messaging_client is reused.
TEST_F(MessagingFixture, RecreateClientChangesRpcId) {
    auto factory = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");

    auto first = factory->create_client(to_string(app_2_->id_));
    ASSERT_NE(first, nullptr);
    auto const first_id = first->get_id();

    auto second = factory->create_client(to_string(app_2_->id_));
    ASSERT_NE(second, nullptr);

    EXPECT_NE(first.get(), second.get()) << "recreate must return a new rpc_client object";
    EXPECT_NE(first_id, second->get_id()) << "recreate must change rpc_client::get_id() for NuRaft stale check";
}

TEST_F(MessagingFixture, DataPathReinitPreservesRpcClientId) {
    auto factory = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");
    auto const peer = app_2_->id_;

    auto first = factory->create_client(to_string(peer));
    ASSERT_NE(first, nullptr);
    auto const first_transport = custom_factory_->cached_transport(peer);
    ASSERT_NE(first_transport, nullptr);

    custom_factory_->force_recreate(true);
    auto refreshed = factory->create_or_reinit_client(peer);
    custom_factory_->force_recreate(false);

    ASSERT_NE(refreshed, nullptr);
    EXPECT_EQ(refreshed.get(), first.get()) << "data-path reinit must preserve the existing wrapper";
    EXPECT_EQ(refreshed->get_id(), first->get_id()) << "data-path reinit must preserve rpc_client identity";
    EXPECT_NE(custom_factory_->cached_transport(peer), first_transport)
        << "data-path reinit must still refresh the shared messaging_client";
}

// Two raft groups share one group_factory. Verify the shared messaging_client
// cache: initial creates share one transport; after group1 replaces it, group2
// must adopt that same refreshed transport on its next create/reinit.
TEST_F(MessagingFixture, SharedGroupFactoryReusesReinitTransport) {
    auto group1 = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");
    auto const group2_id = boost::uuids::random_generator()();
    auto group2 = std::make_shared< mesg_factory >(custom_factory_, group2_id, "test_type");
    auto const peer = app_2_->id_;

    ASSERT_NE(group1->create_client(to_string(peer)), nullptr);
    auto const group1_initial_transport = custom_factory_->cached_transport(peer);
    ASSERT_NE(group1_initial_transport, nullptr);

    ASSERT_NE(group2->create_or_reinit_client(peer), nullptr);
    auto const group2_initial_transport = custom_factory_->cached_transport(peer);
    EXPECT_EQ(group2_initial_transport, group1_initial_transport)
        << "both groups must initially share one messaging_client";

    // Replace the shared transport once (simulates dead connection / bad_service).
    custom_factory_->force_recreate(true);
    ASSERT_NE(group1->create_client(to_string(peer)), nullptr);
    custom_factory_->force_recreate(false);

    auto const group1_refreshed_transport = custom_factory_->cached_transport(peer);
    ASSERT_NE(group1_refreshed_transport, nullptr);
    EXPECT_NE(group1_refreshed_transport, group1_initial_transport)
        << "group1 reinit must publish a new messaging_client into the shared cache";
    EXPECT_EQ(group2_initial_transport, group1_initial_transport)
        << "group2 still observes the original transport until it reinits";

    ASSERT_NE(group2->create_or_reinit_client(peer), nullptr);
    EXPECT_EQ(custom_factory_->cached_transport(peer), group1_refreshed_transport)
        << "group2 reinit must reuse group1's refreshed messaging_client, not allocate another";
}

// Counterpart to SharedGroupFactoryReusesReinitTransport where the DATA PATH
// (not NuRaft recreate_rpc) is the trigger that replaces the shared transport.
// After group1's data-path reinit updates group_factory's cache, group2's next
// reinit must adopt that transport instead of allocating a separate connection.
TEST_F(MessagingFixture, SharedGroupFactoryDataPathUpdatesCache) {
    auto group1 = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");
    auto const group2_id = boost::uuids::random_generator()();
    auto group2 = std::make_shared< mesg_factory >(custom_factory_, group2_id, "test_type");
    auto const peer = app_2_->id_;

    // raft_held simulates NuRaft's peer._rpc after recreate_rpc().
    auto raft_held = group1->create_client(to_string(peer));
    ASSERT_NE(raft_held, nullptr);
    ASSERT_NE(group2->create_or_reinit_client(peer), nullptr);
    auto const initial_transport = custom_factory_->cached_transport(peer);
    ASSERT_NE(initial_transport, nullptr);

    // group1 data-path reinit replaces the shared transport in group_factory.
    custom_factory_->force_recreate(true);
    auto data_path_result = group1->create_or_reinit_client(peer);
    custom_factory_->force_recreate(false);

    ASSERT_NE(data_path_result, nullptr);
    // data-path reinit calls setClient() on the existing wrapper in place, so the
    // object NuRaft holds (raft_held) is the same object that now points to the
    // new transport - no extra send failure needed before NuRaft benefits.
    EXPECT_EQ(data_path_result.get(), raft_held.get())
        << "data-path reinit must update NuRaft's held wrapper in place, not replace it";

    auto const refreshed_transport = custom_factory_->cached_transport(peer);
    ASSERT_NE(refreshed_transport, nullptr);
    EXPECT_NE(refreshed_transport, initial_transport)
        << "data-path reinit must publish a new messaging_client into group_factory cache";

    // group2's next reinit must reuse refreshed_transport, not create a third connection.
    ASSERT_NE(group2->create_or_reinit_client(peer), nullptr);
    EXPECT_EQ(custom_factory_->cached_transport(peer), refreshed_transport)
        << "group2 reinit must adopt the transport already in group_factory, not allocate another";
}
