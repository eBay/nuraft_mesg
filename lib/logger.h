#pragma once

#include <libnuraft/logger.hxx>
#include <sds_logging/logging.h>

SDS_LOGGING_DECL(nuraft)
SDS_LOGGING_DECL(sds_msg)

class sds_logger : public ::nuraft::logger {
    std::string const _group_id;
    std::shared_ptr< sds_logging::logger_t > _custom_logger;

public:
    explicit sds_logger(std::string const& group_id, std::shared_ptr< sds_logging::logger_t > custom_logger) :
            ::nuraft::logger(), _group_id(group_id), _custom_logger(custom_logger) {}

    void set_level(int l) override {
        LOGDEBUGMOD(sds_msg, "Updating level to: {}", l);
        SDS_LOG_LEVEL(nuraft, static_cast< spdlog::level::level_enum >(abs(l - 6)));
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
