#include "test_fixture.ipp"
#include <libnuraft/raft_server_handler.hxx>

class DataServiceFixture : public MessagingFixtureBase {
protected:
    void SetUp() override {
        MessagingFixtureBase::SetUp();
        start(true);
        test_state_mgr::fill_data_vec(cli_buf, 8);
    }

    void TearDown() override {
        MessagingFixtureBase::TearDown();
        for (auto& buf : cli_buf) {
            buf.buf_free();
        }
    }

    io_blob_list_t cli_buf;
    std::string SEND_DATA{"send_data"};
    std::string REQUEST_DATA{"request_data"};
};

TEST_F(DataServiceFixture, BasicTest1) {
    get_random_ports(2u);
    // create new servers
    auto app_4 = std::make_shared< TestApplication >("sm4", ports[3]);
    lookup_map.emplace(app_4->id_, fmt::format("127.0.0.1:{}", ports[3]));
    app_1_->map_peers(lookup_map);
    app_2_->map_peers(lookup_map);
    app_3_->map_peers(lookup_map);
    app_4->map_peers(lookup_map);
    app_4->start(true);
    auto add4 =
        app_1_->instance_->add_member(group_id_, nuraft::srv_config(to_server_id(app_4->id_), to_string(app_4->id_)));
    EXPECT_TRUE(sync_get(std::move(add4)));

    auto app_5 = std::make_shared< TestApplication >("sm5", ports[4]);
    lookup_map.emplace(app_5->id_, fmt::format("127.0.0.1:{}", ports[4]));
    app_1_->map_peers(lookup_map);
    app_2_->map_peers(lookup_map);
    app_3_->map_peers(lookup_map);
    app_4->map_peers(lookup_map);
    app_5->map_peers(lookup_map);
    app_5->start(true);
    auto add5 =
        app_1_->instance_->add_member(group_id_, nuraft::srv_config(to_server_id(app_5->id_), to_string(app_5->id_)));
    EXPECT_TRUE(sync_get(std::move(add5)));

    // create new group
    auto follower_priority = 80;
    auto data_group = boost::uuids::random_generator()();
    (void)app_4->instance_->create_group(data_group, "test_type");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto add1 =
        app_4->instance_->add_member(data_group, nuraft::srv_config(to_server_id(app_1_->id_), 0, to_string(app_1_->id_), "", false, follower_priority));
    EXPECT_TRUE(sync_get(std::move(add1)));
    auto add2 =
        app_4->instance_->add_member(data_group, nuraft::srv_config(to_server_id(app_2_->id_), 0, to_string(app_2_->id_), "", false, follower_priority));
    EXPECT_TRUE(sync_get(std::move(add2)));
    auto add5_2 =
        app_4->instance_->add_member(data_group, nuraft::srv_config(to_server_id(app_5->id_), 0, to_string(app_5->id_), "", true, follower_priority));
    EXPECT_TRUE(sync_get(std::move(add5_2)));
    // check priority
    auto repl_ctx = app_4->state_mgr_map_[data_group]->get_repl_context();
    EXPECT_TRUE(repl_ctx && repl_ctx->is_raft_leader());
    auto peer_info = repl_ctx->get_raft_status();
    for (auto pinfo : peer_info) {
        LOGINFO("endpoint: {}, priority: {}", pinfo.id_, pinfo.priority_);
        if (pinfo.id_ == to_string(app_4->id_)) {
            EXPECT_EQ(pinfo.priority_, 100);
        } else {
            EXPECT_EQ(pinfo.priority_, follower_priority);
        }
        if (pinfo.id_ == to_string(app_5->id_)) {
            EXPECT_TRUE(pinfo.is_learner_);
        }
    }

    auto sm1 = app_1_->state_mgr_map_[group_id_];
    RELEASE_ASSERT(sm1, "Bad pointer!");
    auto sm4_1 = app_4->state_mgr_map_[group_id_];
    RELEASE_ASSERT(sm1, "Bad pointer!");
    auto sm4 = app_4->state_mgr_map_[data_group];
    RELEASE_ASSERT(sm4, "Bad pointer!");
    auto sm5 = app_5->state_mgr_map_[data_group];
    RELEASE_ASSERT(sm5, "Bad pointer!");

    EXPECT_TRUE(sync_get(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, SEND_DATA, cli_buf)));

    EXPECT_TRUE(sync_get(sm5->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, REQUEST_DATA, cli_buf)));

    {
        auto r = sync_get(sm4_1->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, REQUEST_DATA, cli_buf));
        EXPECT_TRUE(r);
        if (r) { test_state_mgr::verify_data(r->response_blob()); }
    }

    EXPECT_TRUE(sync_get(sm1->data_service_request_unidirectional(app_2_->id_, SEND_DATA, cli_buf)));

    // Enumerate the group's peers via the public get_cluster_config (peer_id is the endpoint = uuid string)
    // and send to each by peer_id -- no reach-through to the raw raft_server.
    std::list< nuraft_mesg::replica_config > cluster_config;
    sm1->get_repl_context()->get_cluster_config(cluster_config);
    for (auto const& rc : cluster_config) {
        if (rc.peer_id == to_string(app_1_->id_)) continue;
        LOGINFO("Sending request to peer [{}]", rc.peer_id)
        auto const peer = boost::uuids::string_generator()(rc.peer_id);
        EXPECT_TRUE(sync_get(sm1->data_service_request_bidirectional(peer, REQUEST_DATA, cli_buf)));
    }

    // test big message
    LOGINFO("Starting large object write test")
    io_blob_list_t big_cli_buf;
    test_state_mgr::fill_data_vec_big(big_cli_buf, 4 * 1024 * 1024);
    EXPECT_TRUE(sync_get(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, SEND_DATA, big_cli_buf)));
    LOGINFO("End large object write test")
    LOGINFO("Starting large object read test")
    {
        auto r = sync_get(sm4_1->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, REQUEST_DATA, big_cli_buf));
        EXPECT_TRUE(r);
        if (r) { test_state_mgr::verify_data(r->response_blob()); }
    }
    LOGINFO("End large object read test")
    for (auto& buf : big_cli_buf) {
        buf.buf_free();
    }

    // add a new member to data_service_test_group and check if repl_ctx4 sends data to newly added member
    auto add_3 = app_4->instance_->add_member(data_group, app_3_->id_);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(sync_get(std::move(add_3)));
    EXPECT_TRUE(sync_get(sm4->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, SEND_DATA, cli_buf)));

    // TODO REVIEW THIS
    // test_group: 4 (2 * 1 SEND_DATA) + 6 (1 REQUEST_DATA) + 1 (SEND_DATA to a peer) = 15
    // data_service_test_group: 1 (1 REQUEST_DATA) + 4 (1 SEND_DATA) = 5
    EXPECT_EQ(test_state_mgr::get_server_counter(), 20);
    app_5->instance_->leave_group(data_group);
    app_5->instance_->leave_group(group_id_);
    app_4->instance_->leave_group(data_group);
    app_4->instance_->leave_group(group_id_);
    app_3_->instance_->leave_group(data_group);
    app_2_->instance_->leave_group(data_group);
    app_1_->instance_->leave_group(data_group);
}

TEST_F(DataServiceFixture, BasicTest2) {
    auto sm1 = app_1_->state_mgr_map_[group_id_];
    auto repl_ctx = sm1->get_repl_context();

    EXPECT_TRUE(repl_ctx && repl_ctx->is_raft_leader());
    EXPECT_TRUE(repl_ctx && repl_ctx->raft_leader_id() == to_string(app_1_->id_));
    auto peer_info = repl_ctx->get_raft_status();
    EXPECT_TRUE(peer_info.size() == 3);
    for (auto const& peer : peer_info) {
        std::cout << "Peer ID: " << peer.id_ << " Last Log Idx: " << peer.last_log_idx_
                  << " Last Succ Resp Us: " << peer.last_succ_resp_us_ << " Priority: " << peer.priority_
                  << " Is Learner: " << peer.is_learner_ << " Is New Joiner: " << peer.is_new_joiner_ << std::endl;
        EXPECT_TRUE(peer.id_ == to_string(app_1_->id_) || peer.id_ == to_string(app_2_->id_) ||
                    peer.id_ == to_string(app_3_->id_));
        EXPECT_TRUE(peer.last_log_idx_ == 3);
        if (peer.id_ == to_string(app_1_->id_)) {
            EXPECT_TRUE(peer.last_succ_resp_us_ == 0);
        } else {
            EXPECT_TRUE(peer.last_succ_resp_us_ > 0);
        }
    }

    auto repl_ctx_2 = app_2_->state_mgr_map_[group_id_]->get_repl_context();
    EXPECT_TRUE(repl_ctx_2 && !repl_ctx_2->is_raft_leader());
    EXPECT_TRUE(repl_ctx_2 && repl_ctx_2->raft_leader_id() == to_string(app_1_->id_));
    // if it`s a follower, it should have only one peer info of itself
    EXPECT_TRUE(repl_ctx && repl_ctx_2->get_raft_status().size() == 1);

    auto repl_ctx_3 = app_3_->state_mgr_map_[group_id_]->get_repl_context();
    EXPECT_TRUE(repl_ctx_3 && !repl_ctx_3->is_raft_leader());
    EXPECT_TRUE(repl_ctx_3 && repl_ctx_3->raft_leader_id() == to_string(app_1_->id_));
    EXPECT_TRUE(repl_ctx && repl_ctx_3->get_raft_status().size() == 1);

    std::list< nuraft_mesg::replica_config > cluster_config;
    repl_ctx->get_cluster_config(cluster_config);
    EXPECT_EQ(cluster_config.size(), 3u);
    auto config_set = std::set< std::string >();
    for (auto const& config : cluster_config) {
        config_set.emplace(config.peer_id);
    }
    EXPECT_TRUE(config_set.count(to_string(app_1_->id_)) > 0);
    EXPECT_TRUE(config_set.count(to_string(app_2_->id_)) > 0);
    EXPECT_TRUE(config_set.count(to_string(app_3_->id_)) > 0);
}

TEST_F(DataServiceFixture, NegativeTests) {
    auto sm1 = app_1_->state_mgr_map_[group_id_];
    auto sm2 = app_2_->state_mgr_map_[group_id_];

    // invalid request name — unidirectional to ALL is fire-and-forget, no error
    EXPECT_TRUE(sync_get(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, "invalid_request", cli_buf)));

    {
        auto r = sync_get(sm2->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, "invalid_request", cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(std::error_condition{std::errc::invalid_argument}, r.error());
    }

    // Leader calling data request for a leader
    {
        auto r = sync_get(sm1->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, SEND_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(std::error_condition{std::errc::invalid_argument}, r.error());
    }

    {
        auto r = sync_get(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::LEADER, SEND_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(std::error_condition{std::errc::invalid_argument}, r.error());
    }

    // invalid peer id
    {
        auto r = sync_get(sm1->data_service_request_unidirectional(boost::uuids::random_generator()(), REQUEST_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(nuraft_mesg::make_error_condition(nuraft_mesg::errc::failed), r.error());
    }

    {
        auto r = sync_get(sm1->data_service_request_bidirectional(boost::uuids::random_generator()(), REQUEST_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(nuraft_mesg::make_error_condition(nuraft_mesg::errc::failed), r.error());
    }

    // invalid svr id
    {
        auto r = sync_get(sm1->data_service_request_unidirectional(-1, REQUEST_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(nuraft_mesg::make_error_condition(nuraft_mesg::errc::failed), r.error());
    }

    // unimplemented methods
    {
        auto r = sync_get(sm1->data_service_request_bidirectional(nuraft_mesg::role_regex::ALL, REQUEST_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(std::error_condition{std::errc::invalid_argument}, r.error());
    }

    {
        auto r = sync_get(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::FOLLOWER, REQUEST_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(std::error_condition{std::errc::invalid_argument}, r.error());
    }

    // This should be the last test, this exercises the null-server and null-factory failure paths.
    // Null raft server (factory still present): a make_repl_ctx with a null grpc_server leaves _server null,
    // which the resolve path reports as a failure (no raw _server poke needed -- it's encapsulated now).
    sm2->make_repl_ctx(nullptr, std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type"));
    {
        auto r = sync_get(sm2->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, REQUEST_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(nuraft_mesg::make_error_condition(nuraft_mesg::errc::failed), r.error());
    }

    // mesg factory nullptr
    sm2->make_repl_ctx(nullptr, nullptr);
    {
        auto r = sync_get(sm2->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, REQUEST_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(nuraft_mesg::make_error_condition(nuraft_mesg::errc::failed), r.error());
    }

    {
        auto r = sync_get(sm2->data_service_request_bidirectional(nuraft_mesg::role_regex::ALL, REQUEST_DATA, cli_buf));
        EXPECT_FALSE(r);
        EXPECT_EQ(nuraft_mesg::make_error_condition(nuraft_mesg::errc::failed), r.error());
    }
}

TEST_F(DataServiceFixture, AutoCreateClientTest) {
    // Test that data_service_request_* works correctly when sending to a peer
    // that is in lookup_map but the sender doesn't have a client to it yet
    // This verifies the auto-create client functionality

    get_random_ports(1u);

    // Create app_4 and add it to lookup_map
    auto app_4 = std::make_shared< TestApplication >("sm4", ports[3]);

    // Register the peer in lookup_map (so create_client can find the endpoint)
    lookup_map.emplace(app_4->id_, fmt::format("127.0.0.1:{}", ports[3]));
    app_1_->map_peers(lookup_map);
    app_2_->map_peers(lookup_map);
    app_3_->map_peers(lookup_map);
    app_4->map_peers(lookup_map);
    app_4->start(true);

    // app_1 adds app_4 to the group -> app_1 will have client to app_4
    auto add4 = app_1_->instance_->add_member(group_id_, nuraft::srv_config(to_server_id(app_4->id_), to_string(app_4->id_)));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(sync_get(std::move(add4)));

    auto sm1 = app_1_->state_mgr_map_[group_id_];
    RELEASE_ASSERT(sm1, "Bad pointer for app_1!");
    auto sm4 = app_4->state_mgr_map_[group_id_];
    RELEASE_ASSERT(sm4, "Bad pointer for app_4!");

    // Key test: app_4 sends to app_1
    // At this point, app_4's factory might not have a client to app_1
    // because app_4 was passively added to the group
    // The auto-create functionality should kick in

    // Test 1: data_service_request_unidirectional with auto-created client
    LOGINFO("Testing unidirectional request - should auto-create client if needed");
    EXPECT_TRUE(sync_get(sm4->data_service_request_unidirectional(app_1_->id_, SEND_DATA, cli_buf)))
        << "Unidirectional request should succeed with auto-created client";

    // Test 2: data_service_request_bidirectional with auto-created client
    LOGINFO("Testing bidirectional request - client should already exist from test 1");
    EXPECT_TRUE(sync_get(sm4->data_service_request_bidirectional(app_1_->id_, REQUEST_DATA, cli_buf)))
        << "Bidirectional request should succeed";

    LOGINFO("Auto-create client test passed");

    // Clean up
    app_4->instance_->leave_group(group_id_);
}
