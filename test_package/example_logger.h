#pragma once

#include "cornerstone/raft_core_grpc.hpp"
#include <sds_logging/logging.h>

struct sds_logger : ::cornerstone::logger {
    void debug(const std::string& log_line) override {
        LOGDEBUG("{}", log_line);
    }

    void info(const std::string& log_line) override {
        LOGINFO("{}", log_line);
    }

    void warn(const std::string& log_line) override {
        LOGWARN("{}", log_line);
    }

    void err(const std::string& log_line) override {
       LOGERROR("{}", log_line);
    }

    void set_level(int l) override {
        spdlog::set_level((spdlog::level::level_enum)l);
    }
};

