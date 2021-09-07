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

#include <libnuraft/nuraft.hxx>
#include <sds_logging/logging.h>

#include "raft_types.pb.h"

SDS_LOGGING_DECL(nuraft)

namespace nuraft_grpc {

template < typename T >
using boxed = std::unique_ptr< T >;

template < typename T >
using shared = std::shared_ptr< T >;

} // namespace nuraft_grpc
