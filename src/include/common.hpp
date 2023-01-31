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
//   Common aliases and types.
//
#pragma once

#include <memory>

#include <libnuraft/nuraft.hxx>
#include <sisl/logging/logging.h>
#include <sisl/settings/settings.hpp>
#include "generated/nuraft_mesg_config_generated.h"
#include "proto/raft_types.pb.h"

SISL_LOGGING_DECL(nuraft_mesg)

namespace nuraft_mesg {

template < typename T >
using boxed = std::unique_ptr< T >;

template < typename T >
using shared = std::shared_ptr< T >;

} // namespace nuraft_mesg

SETTINGS_INIT(nuraftmesgcfg::NuraftMesgConfig, nuraft_mesg_config);

#define NURAFT_MESG_CONFIG_WITH(...) SETTINGS(nuraft_mesg_config, __VA_ARGS__)
#define NURAFT_MESG_CONFIG_THIS(...) SETTINGS_THIS(nuraft_mesg_config, __VA_ARGS__)
#define NURAFT_MESG_CONFIG(...) SETTINGS_VALUE(nuraft_mesg_config, __VA_ARGS__)

#define NURAFT_MESG_SETTINGS_FACTORY() SETTINGS_FACTORY(nuraft_mesg_config)
