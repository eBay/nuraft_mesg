#pragma once

#include <sisl/logging/logging.h>
#include <grpcpp/generic/async_generic_service.h>

#include "nuraft_mesg/common.hpp"

#define LOGT(...) LOGTRACEMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGD(...) LOGDEBUGMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGI(...) LOGINFOMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGW(...) LOGWARNMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGE(...) LOGERRORMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGC(...) LOGCRITICALMOD(nuraft_mesg, ##__VA_ARGS__)

namespace nuraft_mesg {

// generic rpc server looks up rpc name in a map and calls the corresponding callback. To avoid another lookup in this
// layer, we registed one callback for each (group_id, request_name) pair. The rpc_name is their concatenation.
[[maybe_unused]] static std::string get_generic_method_name(std::string const& request_name,
                                                            group_id_t const& group_id) {
    return fmt::format("{}|{}", request_name, group_id);
}

} // namespace nuraft_mesg
