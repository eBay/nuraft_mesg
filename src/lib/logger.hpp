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

#include "common_lib.hpp"

class nuraft_mesg_logger : public ::nuraft::logger {
    nuraft_mesg::group_id_t const _group_id;
    std::shared_ptr< sisl::logging::logger_t > _custom_logger;

public:
    explicit nuraft_mesg_logger(nuraft_mesg::group_id_t const& group_id,
                                std::shared_ptr< sisl::logging::logger_t > custom_logger) :
            ::nuraft::logger(), _group_id(group_id), _custom_logger(custom_logger) {}

    void set_level(int) override {}

    void put_details(int level, const char* source_file, const char* func_name, size_t line_number,
                     const std::string& log_line) override {
        switch (level) {
        case 1: {
            if (LEVELCHECK(nuraft_mesg, spdlog::level::level_enum::critical))
                sisl::logging::GetLogger()->critical("[{}:{}:{}] {} [group={}]", file_name(source_file), line_number,
                                                     func_name, log_line, _group_id);
        } break;
        case 2: {
            if (LEVELCHECK(nuraft_mesg, spdlog::level::level_enum::err))
                sisl::logging::GetLogger()->error("[{}:{}:{}] {} [group={}]", file_name(source_file), line_number,
                                                  func_name, log_line, _group_id);
        } break;
        case 3: {
            if (LEVELCHECK(nuraft_mesg, spdlog::level::level_enum::warn))
                sisl::logging::GetLogger()->warn("[{}:{}:{}] {} [group={}]", file_name(source_file), line_number,
                                                 func_name, log_line, _group_id);
        } break;
        case 4: {
            if (LEVELCHECK(nuraft_mesg, spdlog::level::level_enum::info))
                _custom_logger->info("[{}:{}:{}] {} [group={}]", file_name(source_file), line_number, func_name,
                                     log_line, _group_id);
        } break;
        case 5: {
            if (LEVELCHECK(nuraft_mesg, spdlog::level::level_enum::debug))
                _custom_logger->debug("[{}:{}:{}] {} [group={}]", file_name(source_file), line_number, func_name,
                                      log_line, _group_id);
        } break;
        default: {
            if (LEVELCHECK(nuraft_mesg, spdlog::level::level_enum::trace))
                _custom_logger->trace("[{}:{}:{}] {} [group={}]", file_name(source_file), line_number, func_name,
                                      log_line, _group_id);
        } break;
        }
    }
};
