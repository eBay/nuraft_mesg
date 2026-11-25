#pragma once
#include <cstddef>
#include <sisl/settings/settings.hpp>
#include "lib/generated/nuraft_mesg_config_generated.h"

SETTINGS_INIT(nuraftmesgcfg::NuraftMesgConfig, nuraft_mesg_config);

#define NURAFT_MESG_CONFIG_WITH(...) SETTINGS(nuraft_mesg_config, __VA_ARGS__)
#define NURAFT_MESG_CONFIG_THIS(...) SETTINGS_THIS(nuraft_mesg_config, __VA_ARGS__)
#define NURAFT_MESG_CONFIG(...) SETTINGS_VALUE(nuraft_mesg_config, __VA_ARGS__)

#define NURAFT_MESG_SETTINGS_FACTORY() SETTINGS_FACTORY(nuraft_mesg_config)

namespace nuraft_mesg {
// The size of proto_service's I/O thread pool: io_thread_pool_size, or 4 threads when it isn't set.
inline size_t io_pool_size() {
    auto const size = NURAFT_MESG_CONFIG(io_thread_pool_size);
    return size > 0 ? size : 4;
}
} // namespace nuraft_mesg
