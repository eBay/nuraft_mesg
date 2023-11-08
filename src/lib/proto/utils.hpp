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
//   Common transformations
//
#pragma once

#include "nuraft_mesg/mesg_factory.hpp"
#include "raft_types.pb.h"

#include "lib/common_lib.hpp"

namespace nuraft_mesg {

inline RCMsgBase* fromBaseRequest(nuraft::msg_base const& rcbase) {
    auto base = new RCMsgBase;
    base->set_term(rcbase.get_term());
    base->set_src(rcbase.get_src());
    base->set_dest(rcbase.get_dst());
    base->set_type(rcbase.get_type());
    return base;
}

} // namespace nuraft_mesg
