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
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(std::move(add4).get());

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
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(std::move(add5).get());

    // create new group
    auto follower_priority = 80;
    auto data_group = boost::uuids::random_generator()();
    app_4->instance_->create_group(data_group, "test_type");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto add1 =
        app_4->instance_->add_member(data_group, nuraft::srv_config(to_server_id(app_1_->id_), 0, to_string(app_1_->id_), "", false, follower_priority));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(std::move(add1).get());
    auto add2 =
        app_4->instance_->add_member(data_group, nuraft::srv_config(to_server_id(app_2_->id_), 0, to_string(app_2_->id_), "", false, follower_priority));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(std::move(add2).get());
    add5 =
        app_4->instance_->add_member(data_group, nuraft::srv_config(to_server_id(app_5->id_), 0, to_string(app_5->id_), "", false, follower_priority));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(std::move(add5).get());
    std::vector< std::shared_ptr< nuraft::srv_config > > config_out;
    // check priority
    app_4->instance_->get_srv_config_all(data_group, config_out);
    for (auto cfg : config_out) {
        LOGINFO("Server id: {}, endpoint: {}, priority: {}", cfg->get_id(), cfg->get_endpoint(), cfg->get_priority());
        if (cfg->get_id() == to_server_id(app_4->id_)) {
            EXPECT_EQ(cfg->get_priority(), 100);
        } else {
            EXPECT_EQ(cfg->get_priority(), follower_priority);
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

    std::vector< NullAsyncResult > results;
    results.push_back(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, SEND_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasValue());
                              return folly::Unit();
                          }));
    results.push_back(sm5->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, REQUEST_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasValue());
                              return folly::Unit();
                          }));

    results.push_back(sm4_1->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, REQUEST_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              test_state_mgr::verify_data(e.value().response_blob());
                              return folly::Unit();
                          }));

    results.push_back(
        sm1->data_service_request_unidirectional(app_2_->id_, SEND_DATA, cli_buf).deferValue([](auto e) -> NullResult {
            EXPECT_TRUE(e.hasValue());
            return folly::Unit();
        }));

    auto repl_ctx1 = sm1->get_repl_context();
    for (auto svr : repl_ctx1->_server->get_config()->get_servers()) {
        if (svr->get_endpoint() == to_string(app_1_->id_)) continue;
        LOGINFO("Sending request to server [{}]", svr->get_id())
        results.push_back(sm1->data_service_request_bidirectional(svr->get_id(), REQUEST_DATA, cli_buf)
                              .deferValue([](auto e) -> NullResult {
                                  EXPECT_TRUE(e.hasValue());
                                  return folly::Unit();
                              }));
    }

    folly::collectAll(results).via(folly::getGlobalCPUExecutor()).get();

    // test big message
    LOGINFO("Starting large object write test")
    io_blob_list_t big_cli_buf;
    test_state_mgr::fill_data_vec_big(big_cli_buf, 4 * 1024 * 1024);
    sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, SEND_DATA, big_cli_buf)
        .deferValue([](auto e) -> NullResult {
            EXPECT_TRUE(e.hasValue());
            return folly::Unit();
        })
        .get();
    LOGINFO("End large object write test")
    LOGINFO("Starting large object read test")

    sm4_1->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, REQUEST_DATA, big_cli_buf)
        .deferValue([](auto e) -> NullResult {
            EXPECT_TRUE(e.hasValue());
            test_state_mgr::verify_data(e.value().response_blob());
            return folly::Unit();
        })
        .get();
    LOGINFO("End large object read test")
    for (auto& buf : big_cli_buf) {
        buf.buf_free();
    }

    // add a new member to data_service_test_group and check if repl_ctx4 sends data to newly added member
    auto add_3 = app_4->instance_->add_member(data_group, app_3_->id_);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(std::move(add_3).get());
    sm4->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, SEND_DATA, cli_buf)
        .deferValue([](auto e) -> folly::Unit {
            EXPECT_TRUE(e.hasValue());
            return folly::Unit();
        })
        .get();

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
                  << " Last Succ Resp Us: " << peer.last_succ_resp_us_ << std::endl;
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
    std::vector< NullAsyncResult > results;

    // invalid request name
    results.push_back(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, "invalid_request", cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              // unidirectional request to ALL is fire and forget, dosen't return an error
                              EXPECT_TRUE(e.hasValue());
                              return folly::Unit();
                          }));

    results.push_back(
        sm2->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, "invalid_request", cli_buf)
            .deferValue([](auto e) -> NullResult {
                EXPECT_TRUE(e.hasError());
                EXPECT_EQ(nuraft::cmd_result_code::BAD_REQUEST, e.error());
                return folly::Unit();
            }));

    // Leader calling data request for a leader
    results.push_back(sm1->data_service_request_bidirectional(nuraft_mesg::role_regex::LEADER, SEND_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasError());
                              EXPECT_EQ(nuraft::cmd_result_code::BAD_REQUEST, e.error());
                              return folly::Unit();
                          }));

    results.push_back(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::LEADER, SEND_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasError());
                              EXPECT_EQ(nuraft::cmd_result_code::BAD_REQUEST, e.error());
                              return folly::Unit();
                          }));

    // invalid peer id
    results.push_back(
        sm1->data_service_request_unidirectional(boost::uuids::random_generator()(), REQUEST_DATA, cli_buf)
            .deferValue([](auto e) -> NullResult {
                EXPECT_TRUE(e.hasError());
                EXPECT_EQ(nuraft::cmd_result_code::SERVER_NOT_FOUND, e.error());
                return folly::Unit();
            }));

    results.push_back(sm1->data_service_request_bidirectional(boost::uuids::random_generator()(), REQUEST_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasError());
                              EXPECT_EQ(nuraft::cmd_result_code::SERVER_NOT_FOUND, e.error());
                              return folly::Unit();
                          }));

    // invalid svr id
    results.push_back(
        sm1->data_service_request_unidirectional(-1, REQUEST_DATA, cli_buf).deferValue([](auto e) -> NullResult {
            EXPECT_TRUE(e.hasError());
            EXPECT_EQ(nuraft::cmd_result_code::SERVER_NOT_FOUND, e.error());
            return folly::Unit();
        }));

    // unimplemented methods
    results.push_back(sm1->data_service_request_bidirectional(nuraft_mesg::role_regex::ALL, REQUEST_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasError());
                              EXPECT_EQ(nuraft::cmd_result_code::BAD_REQUEST, e.error());
                              return folly::Unit();
                          }));

    results.push_back(sm1->data_service_request_unidirectional(nuraft_mesg::role_regex::FOLLOWER, REQUEST_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasError());
                              EXPECT_EQ(nuraft::cmd_result_code::BAD_REQUEST, e.error());
                              return folly::Unit();
                          }));

    // This should be the last test, this sets the raft server and mesg_factory to nullptr
    auto repl_ctx = sm2->get_repl_context();

    // raft server nullptr
    repl_ctx->_server = nullptr;
    results.push_back(sm2->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, REQUEST_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasError());
                              EXPECT_EQ(nuraft::cmd_result_code::SERVER_NOT_FOUND, e.error());
                              return folly::Unit();
                          }));

    // mesg factory nullptr
    sm2->make_repl_ctx(nullptr, nullptr);
    results.push_back(sm2->data_service_request_unidirectional(nuraft_mesg::role_regex::ALL, REQUEST_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasError());
                              EXPECT_EQ(nuraft::cmd_result_code::SERVER_NOT_FOUND, e.error());
                              return folly::Unit();
                          }));

    results.push_back(sm2->data_service_request_bidirectional(nuraft_mesg::role_regex::ALL, REQUEST_DATA, cli_buf)
                          .deferValue([](auto e) -> NullResult {
                              EXPECT_TRUE(e.hasError());
                              EXPECT_EQ(nuraft::cmd_result_code::SERVER_NOT_FOUND, e.error());
                              return folly::Unit();
                          }));

    folly::collectAll(results).via(folly::getGlobalCPUExecutor()).get();
}
