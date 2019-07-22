///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//

#pragma once

#include <sds_logging/logging.h>

SDS_LOGGING_DECL(nuraft)

struct sds_logger : ::nuraft::logger {
    void debug(const std::string& log_line) override {
        LOGDEBUGMOD(nuraft, "{}", log_line);
    }

    void info(const std::string& log_line) override {
        LOGINFOMOD(nuraft, "{}", log_line);
    }

    void warn(const std::string& log_line) override {
        LOGWARNMOD(nuraft, "{}", log_line);
    }

    void err(const std::string& log_line) override {
       LOGERRORMOD(nuraft, "{}", log_line);
    }

    void set_level(int l) override {
        LOGINFOMOD(nuraft, "Updating level to: {}", l);
        SDS_LOG_LEVEL(nuraft, static_cast<spdlog::level::level_enum>(abs(l - 6)));
    }

    void put_details(int level,
                     const char* source_file,
                     const char* func_name,
                     size_t line_number,
                     const std::string& log_line) override {
        switch(level) {
        case 1: { LOGCRITICALMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        case 2: { LOGERRORMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        case 3: { LOGWARNMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        case 4: { LOGINFOMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        case 5: { LOGDEBUGMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        default: { LOGTRACEMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line); } break;;
        }
    }
};

