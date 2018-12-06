///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Common transformations
//

#pragma once

#include "common.hpp"

namespace raft_core {

inline
RCMsgBase*
fromBaseRequest(cstn::msg_base const& rcbase) {
   auto base = new RCMsgBase;
   base->set_term(rcbase.get_term());
   base->set_src(rcbase.get_src());
   base->set_dest(rcbase.get_dst());
   base->set_type(rcbase.get_type());
   return base;
}

}
