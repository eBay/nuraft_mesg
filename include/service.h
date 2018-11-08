#pragma once

#include <map>
#include <shared_mutex>

#include "factory.h"
#include "messaging_service.grpc.pb.h"

namespace sds_messaging {

namespace sdsmsg = ::sds::messaging;

using lock_type = std::shared_mutex;

template<class Factory, class StateMgr>
struct grpc_service : public sds::messaging::Messaging::Service
{
   using get_state_machine_cb = std::function<std::error_condition(group_name_t const&, cstn::ptr<cstn::state_machine>&)>;
   grpc_service(uint32_t const unique_id,
                cstn::ptr<cstn::logger>&& logger,
                get_state_machine_cb get_state_machine) :
         sds::messaging::Messaging::Service(),
         _uuid(unique_id),
         _logger(std::move(logger)),
         _get_sm(get_state_machine),
         _scheduler(cstn::cs_new<cstn::asio_service>())
   { }

   ::grpc::Status RaftStep(::grpc::ServerContext *context,
                           sdsmsg::RaftGroupMsg const *request,
                           sdsmsg::RaftGroupMsg *response) override {
      auto const &group_name = request->group_name();
      response->set_group_name(group_name);

      auto const& base = request->message().base();
      LOGTRACEMOD(sds_msg, "Received [{}] from: [{}] to: [{}] Group: [{}]",
                     base.type(),
                     base.src(),
                     base.dest(),
                     group_name
                     );

      if (cstn::join_cluster_request == base.type()) {
        joinRaftGroup(group_name);
      }

      shared<cstn::grpc_service> server;
      {
         std::shared_lock<lock_type> rl(_raft_servers_lock);
         if (auto it = _raft_servers.find(group_name);
               _raft_servers.end() != it) {
            server = it->second;
         }
      }
      ::grpc::Status status;
      if (server) {
         auto raft_response = response->mutable_message();
         try {
           status = server->step(context, &request->message(), raft_response);
         if (!status.ok()) {
           LOGWARNMOD(sds_msg, "Response: [{}]: {}", status.error_code(), status.error_message());
         }
         } catch (std::runtime_error& rte) {
           LOGERRORMOD(sds_msg, "Caught exception during step(): {}", rte.what());
           status = ::grpc::Status(::grpc::StatusCode::ABORTED, rte.what());
         }
      } else {
         status = ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "RaftGroup missing: {}", group_name);
         LOGWARNMOD(sds_msg, "Missing RAFT group: {}", group_name);
      }
      return status;
   }

   void joinRaftGroup(group_name_t const& group_name) {
      if (0 < _raft_servers.count(group_name))
        return;
      LOGINFOMOD(sds_msg, "Joining RAFT group: {}", group_name);

      cstn::ptr<cstn::state_machine> sm;
      if (auto err = _get_sm(group_name, sm); err) {
         LOGERRORMOD(sds_msg, "Could not create StateMachine for Group[{}]: {}", group_name, err.message());
         return;
      }

      // State manager (RAFT log store, config).
      cstn::ptr<cstn::state_mgr> smgr(cstn::cs_new<StateMgr>(_uuid, group_name));

      // Parameters.
      auto params = new cstn::raft_params();
      params->with_election_timeout_lower(200)
              .with_election_timeout_upper(400)
              .with_hb_interval(100)
              .with_max_append_size(100)
              .with_rpc_failure_backoff(50);

      cstn::ptr<cstn::rpc_client_factory> rpc_cli_factory(cstn::cs_new<Factory>(group_name, _uuid));

      cstn::ptr<cstn::rpc_listener> listener;
      auto ctx = new cstn::context(smgr, sm, listener, _logger, rpc_cli_factory, _scheduler, params);
      auto service = cstn::cs_new<cstn::grpc_service>(cstn::cs_new<cstn::raft_server>(ctx));

      std::unique_lock<lock_type> lck(_raft_servers_lock);
      _raft_servers.emplace(std::make_pair(group_name, service));
   }

 private:
   uint32_t const                                     _uuid;
   cstn::ptr<cstn::logger>                            _logger;
   get_state_machine_cb                               _get_sm;
   lock_type                                          _raft_servers_lock;
   std::map<group_name_t, shared<cstn::grpc_service>> _raft_servers;
   cstn::ptr<cstn::delayed_task_scheduler>            _scheduler;
};

}
