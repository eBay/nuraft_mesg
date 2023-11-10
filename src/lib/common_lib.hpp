#pragma once

#include <sisl/logging/logging.h>
#include <sisl/metrics/metrics.hpp>

#include "nuraft_mesg/common.hpp"

#define LOGT(...) LOGTRACEMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGD(...) LOGDEBUGMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGI(...) LOGINFOMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGW(...) LOGWARNMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGE(...) LOGERRORMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGC(...) LOGCRITICALMOD(nuraft_mesg, ##__VA_ARGS__)

namespace nuraft_mesg {
class group_metrics : public sisl::MetricsGroupWrapper {
public:
    explicit group_metrics(group_id_t const& group_id) :
            sisl::MetricsGroupWrapper("RAFTGroup", to_string(group_id).c_str()) {
        REGISTER_COUNTER(group_steps, "Total group messages received", "raft_group", {"op", "step"});
        REGISTER_COUNTER(group_sends, "Total group messages sent", "raft_group", {"op", "send"});
        register_me_to_farm();
    }

    ~group_metrics() { deregister_me_from_farm(); }
};
} // namespace nuraft_mesg
