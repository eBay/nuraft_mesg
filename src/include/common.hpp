///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Common aliases and types.
//

#pragma once

#include <memory>

#include <cornerstone.hxx>
#include <sds_logging/logging.h>

#include "raft_types.pb.h"

SDS_LOGGING_DECL(raft_core)

namespace raft_core {

namespace cstn = cornerstone;

template<typename T>
using boxed = std::unique_ptr<T>;

template<typename T>
using shared = std::shared_ptr<T>;

}
