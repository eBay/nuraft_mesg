#pragma once

#include <sisl/logging/logging.h>

#include "nuraft_mesg/common.hpp"

#define LOGT(...) LOGTRACEMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGD(...) LOGDEBUGMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGI(...) LOGINFOMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGW(...) LOGWARNMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGE(...) LOGERRORMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGC(...) LOGCRITICALMOD(nuraft_mesg, ##__VA_ARGS__)
