#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

inline const std::string request_manager_ip = "127.0.0.1";
inline const uint32_t request_manager_port = 8002;
inline const std::string request_manager_target_for_sim_server = "/result";
inline const std::string request_manager_target_for_app = "/submit";

inline const std::string sim_server_ip = "127.0.0.1";
inline const std::string sim_server_port = "8003";
inline const std::string sim_server_target = "/submit";

inline const std::string app_target = "/result";
