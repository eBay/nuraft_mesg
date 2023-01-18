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

#include <libnuraft/logger.hxx>
#include <sisl/logging/logging.h>

SISL_LOGGING_DECL(nuraft)
SISL_LOGGING_DECL(nuraft_mesg)

class nuraft_mesg_logger : public ::nuraft::logger {
    std::string const _group_id;
    std::shared_ptr< sisl::logging::logger_t > _custom_logger;

public:
    explicit nuraft_mesg_logger(std::string const& group_id, std::shared_ptr< sisl::logging::logger_t > custom_logger) :
            ::nuraft::logger(), _group_id(group_id), _custom_logger(custom_logger) {}

    void set_level(int l) override {
        LOGDEBUGMOD(nuraft_mesg, "Updating level to: {}", l);
        SISL_LOG_LEVEL(nuraft, static_cast< spdlog::level::level_enum >(abs(l - 6)));
    }

    void put_details(int level, const char* source_file, const char* func_name, size_t line_number,
                     const std::string& log_line) override {
        auto const mesg =
            fmt::format("[vol={}] {}:{}#{} : {}", _group_id, file_name(source_file), func_name, line_number, log_line);
        switch (level) {
        case 1:
            [[fallthrough]];
        case 2: {
            LOGERRORMOD_USING_LOGGER(nuraft, _custom_logger, "ERROR {}", mesg);
        } break;
            ;
        case 3: {
            LOGWARNMOD_USING_LOGGER(nuraft, _custom_logger, "WARNING {}", mesg);
        } break;
            ;
        case 4: {
            LOGINFOMOD_USING_LOGGER(nuraft, _custom_logger, "INFO {}", mesg);
        } break;
            ;
        case 5: {
            LOGDEBUGMOD_USING_LOGGER(nuraft, _custom_logger, "DEBUG {}", mesg);
        } break;
            ;
        default: {
            LOGTRACEMOD_USING_LOGGER(nuraft, _custom_logger, "TRACE {}", mesg);
        } break;
            ;
        }
    }
};
