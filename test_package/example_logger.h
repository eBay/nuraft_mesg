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

#pragma once

#include <sisl/logging/logging.h>

SISL_LOGGING_DECL(nuraft)

struct example_logger : ::nuraft::logger {
    void debug(const std::string& log_line) override { LOGDEBUGMOD(nuraft, "{}", log_line); }

    void info(const std::string& log_line) override { LOGINFOMOD(nuraft, "{}", log_line); }

    void warn(const std::string& log_line) override { LOGWARNMOD(nuraft, "{}", log_line); }

    void err(const std::string& log_line) override { LOGERRORMOD(nuraft, "{}", log_line); }

    void set_level(int l) override {
        LOGINFOMOD(nuraft, "Updating level to: {}", l);
        SISL_LOG_LEVEL(nuraft, static_cast< spdlog::level::level_enum >(abs(l - 6)));
    }

    void put_details(int level, const char* source_file, const char* func_name, size_t line_number,
                     const std::string& log_line) override {
        switch (level) {
        case 1: {
            LOGCRITICALMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line);
        } break;
            ;
        case 2: {
            LOGERRORMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line);
        } break;
            ;
        case 3: {
            LOGWARNMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line);
        } break;
            ;
        case 4: {
            LOGINFOMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line);
        } break;
            ;
        case 5: {
            LOGDEBUGMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line);
        } break;
            ;
        default: {
            LOGTRACEMOD(nuraft, "{}:{}#{} : {}", file_name(source_file), func_name, line_number, log_line);
        } break;
            ;
        }
    }
};
