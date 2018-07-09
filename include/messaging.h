#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>

#include <cornerstone/raft_core_grpc.hpp>
#include <sds_logging/logging.h>

#include "messaging_service.grpc.pb.h"

SDS_LOGGING_DECL(sds_msg)

namespace sds_messaging {

namespace sdsmsg = ::sds::messaging;
namespace cstn = ::cornerstone;

using group_id_t = std::string;

using lock_type = std::shared_mutex;

template<typename T>
using shared = std::shared_ptr<T>;

struct sds_messaging : public cstn::grpc_client {
   sds_messaging(shared<::grpc::ChannelInterface> channel,
               group_id_t const& grp_id) :
         stub_(sdsmsg::Messaging::NewStub(channel)),
         group_id(grp_id)
   {}

   ::grpc::Status send(::grpc::ClientContext *ctx,
                       raft_core::RaftMessage const &message,
                       raft_core::RaftMessage *response) override {
      sdsmsg::RaftGroupMsg group_msg;

      LOGTRACEMOD(sds_msg, "Sending [{}] from: [{}] to: [{}] Group: [{}]",
                  message.base().type(),
                  message.base().src(),
                  message.base().dest(),
                  group_id);
      group_msg.set_group_id(group_id);
      group_msg.mutable_message()->CopyFrom(message);

      sdsmsg::RaftGroupMsg group_rsp;
      auto status = stub_->RaftStep(ctx, group_msg, &group_rsp);

      if (!status.ok()) {
         LOGERRORMOD(sds_msg, "Send Status: {}", status.error_message());
      }
      response->CopyFrom(group_rsp.message());

      return status;
   }

 private:
   std::unique_ptr<typename sdsmsg::Messaging::Stub> stub_;
   group_id_t const group_id;
};

struct grpc_factory : public cstn::rpc_client_factory {
   explicit grpc_factory(group_id_t const& grp_id) :
         cstn::rpc_client_factory(),
         group_id(grp_id)
   { }

   cstn::ptr<cstn::rpc_client> create_client(const std::string &client) override {
      LOGDEBUGMOD(sds_msg, "Creating client for [{}] on group: [{}]", client, group_id);
      auto endpoint = lookupEndpoint(client);
      return std::make_shared<sds_messaging>(::grpc::CreateChannel(endpoint,
                                                                 grpc::InsecureChannelCredentials()),
                                           group_id);
   }

   virtual std::string lookupEndpoint(std::string const& client) = 0;

 private:
   group_id_t const group_id;
};

template<class Factory, class StateMachine, class StateMgr>
struct grpc_service :
      public sdsmsg::Messaging::Service
{
   grpc_service(std::string const& unique_id, cstn::ptr<cstn::logger>&& logger) :
         sdsmsg::Messaging::Service(),
         uuid(unique_id),
         logger(std::move(logger)),
         scheduler(cstn::cs_new<cstn::asio_service>())
   { }

   ::grpc::Status JoinRaftGroup(::grpc::ServerContext *context,
                                sdsmsg::JoinGroupMsg const *request,
                                sdsmsg::JoinGroupResult *response) override
   {
      auto const& members = request->service_group().members();
      // FIXME: do not use offset for this id...something else
      auto server_id {1};
      for (auto i = members.begin(); members.end() != i; ++i) {
         auto const& member = i->node();
         if (uuid == member.uuid()) {
            joinRaftGroup(server_id, request->group_id());
            return ::grpc::Status();
         }
         ++server_id;
      }
      return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "Not a member");
   }

   ::grpc::Status RaftStep(::grpc::ServerContext *context,
                           sdsmsg::RaftGroupMsg const *request,
                           sdsmsg::RaftGroupMsg *response) override {
      auto const &group_id = request->group_id();
      response->set_group_id(group_id);

      auto const& base = request->message().base();
      LOGTRACEMOD(sds_msg, "Stepping [{}] from: [{}] to: [{}] Group: [{}]",
                  base.type(),
                  base.src(),
                  base.dest(),
                  group_id);

      shared<cstn::grpc_service> server;
      {
         std::shared_lock<lock_type> rl(raft_servers_lock);
         if (auto it = raft_servers.find(group_id);
               raft_servers.end() != it) {
            server = it->second;
         }
      }
      ::grpc::Status status;
      if (server) {
         auto raft_response = response->mutable_message();
         status = server->step(context, &request->message(), raft_response);
         LOGTRACEMOD(sds_msg, "Response: {}", status.error_message());
      } else {
         status = ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "RaftGroup missing");
         LOGERRORMOD(sds_msg, "Missing RAFT group: {}", group_id);
      }
      return status;
   }

   void joinRaftGroup(uint32_t const srv_id, group_id_t const& group_id) {
      cstn::ptr<cstn::rpc_client_factory> rpc_cli_factory(cstn::cs_new<Factory>(group_id));
      // State manager (RAFT log store, config).
      cstn::ptr<cstn::state_mgr> smgr(cstn::cs_new<StateMgr>(srv_id, group_id));

      // Parameters.
      auto params = new cstn::raft_params();
      (*params).with_election_timeout_lower(200)
            .with_election_timeout_upper(400)
            .with_hb_interval(100)
            .with_max_append_size(100)
            .with_rpc_failure_backoff(50);

      cstn::ptr<cstn::rpc_listener> listener;
      cstn::ptr<cstn::state_machine> sm(cstn::cs_new<StateMachine>());
      auto ctx = new cstn::context(smgr,
                                   sm,
                                   listener,
                                   logger,
                                   rpc_cli_factory,
                                   scheduler,
                                   params);
      auto service = std::make_shared<cstn::grpc_service>();
      service->registerRaftCore(cstn::cs_new<cstn::raft_server>(ctx));

      std::unique_lock<lock_type> lck(raft_servers_lock);
      raft_servers.emplace(std::make_pair(group_id, service));
   }

 private:
   std::string const                                  uuid;
   cstn::ptr<cstn::logger>                            logger;
   lock_type                                          raft_servers_lock;
   std::map<group_id_t, shared<cstn::grpc_service>>   raft_servers;
   cstn::ptr<cstn::delayed_task_scheduler>            scheduler;
};
}
