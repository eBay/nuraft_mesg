///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//

#pragma once

#include <sds_logging/logging.h>

SDS_LOGGING_DECL(nupillar)

struct sds_logger : ::nupillar::logger {
    void debug(const std::string& log_line) override {
        LOGDEBUGMOD(nupillar, "{}", log_line);
    }

    void info(const std::string& log_line) override {
        LOGINFOMOD(nupillar, "{}", log_line);
    }

    void warn(const std::string& log_line) override {
        LOGWARNMOD(nupillar, "{}", log_line);
    }

    void err(const std::string& log_line) override {
       LOGERRORMOD(nupillar, "{}", log_line);
    }

    void set_level(int l) override {
        LOGINFOMOD(nupillar, "Updating level to: {}", l);
        SDS_LOG_LEVEL(nupillar, static_cast<spdlog::level::level_enum>(abs(l - 6)));
    }

    void put_details(int level,
                     const char* source_file,
                     const char* func_name,
                     size_t line_number,
                     const std::string& log_line) override {
        switch(level) {
        case 1: { LOGCRITICALMOD(nupillar, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        case 2: { LOGERRORMOD(nupillar, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        case 3: { LOGWARNMOD(nupillar, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        case 4: { LOGINFOMOD(nupillar, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        case 5: { LOGDEBUGMOD(nupillar, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        default: { LOGTRACEMOD(nupillar, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        }
    }
};

