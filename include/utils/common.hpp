#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "Logger.hpp"

// Securely execute the system command and obtain the exit code.
int safe_system(const std::string &command)
{
    int status = std::system(command.c_str());
    if (status == -1)
    {
        SPDLOG_LOGGER_ERROR(Logger::instance(), "執行失敗: {}", command);
        return -1;
    }
    else if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        SPDLOG_LOGGER_ERROR(Logger::instance(), "被 signal {} 終止: {}", WTERMSIG(status), command);
        return -1;
    }
    else
    {
        SPDLOG_LOGGER_ERROR(Logger::instance(), "異常終止: {}", command);
        return -1;
    }
}

inline std::string error_response_body(std::string error)
{
    return nlohmann::json{{"error", error}}.dump();
}

inline std::string message_response_body(std::string message)
{
    return nlohmann::json{{"status", message}}.dump();
}
