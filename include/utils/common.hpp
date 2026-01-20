/*
 * Copyright (c) 2025-present
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * NDTwin core contributors (as of January 15, 2026):
 *     Prof. Shie-Yuan Wang <National Yang Ming Chiao Tung University; CITI, Academia Sinica>
 *     Ms. Xiang-Ling Lin <CITI, Academia Sinica>
 *     Mr. Po-Yu Juan <CITI, Academia Sinica>
 *     Mr. Tsu-Li Mou <CITI, Academia Sinica> 
 *     Mr. Zhen-Rong Wu <National Taiwan Normal University>
 *     Mr. Ting-En Chang <University of Wisconsin, Milwaukee>
 *     Mr. Yu-Cheng Chen <National Yang Ming Chiao Tung University>
 */

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
