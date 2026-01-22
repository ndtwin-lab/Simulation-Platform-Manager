#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

struct LogConfig
{
    bool enableFile = false;
    spdlog::level::level_enum level = spdlog::level::info;
};

class Logger
{
  public:
    static spdlog::level::level_enum parse_level(const std::string &name);
    static LogConfig parse_cli_args(int argc, char *argv[]);
    static void init(const LogConfig &cfg);
    static std::shared_ptr<spdlog::logger> instance();

  private:
    static std::shared_ptr<spdlog::logger> m_logger;
};