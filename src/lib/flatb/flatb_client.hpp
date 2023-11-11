/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/

// Brief:
//   Implements cornerstone's rpc_client::send(...) routine to translate
// and execute the call over gRPC asynchrously.
//
#pragma once

#include "lib/client.hpp"

namespace flatbuffers {
template < typename T >
class Offset;
}

namespace nuraft_mesg {

class Request;
class Response;

class grpc_flatb_client : public grpc_base_client {
public:
    using handle_resp = std::function< void(Response&, ::grpc::Status&) >;
    using grpc_base_client::grpc_base_client;
    virtual void send(flatbuffers::Offset< Request > const& request, handle_resp complete) = 0;
};

} // namespace nuraft_mesg
