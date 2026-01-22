#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

std::string app_id = "power";

// inline const std::string app_ip = "127.0.0.1";
inline const std::string app_ip = "10.10.10.251";
inline const uint32_t app_port = 8001;
inline const std::string app_target = "/result";

// inline const std::string ndt_ip = "127.0.0.1";
inline const std::string ndt_ip = "10.10.10.250";
inline const std::string ndt_port = "8000";
inline const std::string ndt_target = "/ndt/app_register";

// inline const std::string request_manager_ip = "127.0.0.1";
inline const std::string request_manager_ip = "10.10.10.250";
inline const std::string request_manager_port = "8000";
inline const std::string request_manager_target_for_app = "/ndt/received_a_simulation_case";

// inline const std::string nfs_server_ip = "127.0.0.1";
inline const std::string nfs_server_ip = "10.10.10.250";
inline const fs::path nfs_mnt_dir = "/mnt/nfs/app";

inline const fs::path input_filename = "input";

inline std::string mount_nfs_command(const std::string& app_id)
{
    return "mount -t nfs " + nfs_server_ip + ":" + "/srv/nfs/sim/" + app_id + " " + nfs_mnt_dir.string();
}

inline std::string unmount_nfs_command()
{
    return "umount " + nfs_mnt_dir.string();
}

inline fs::path abs_input_file_path(
    const std::string &simulator,
    const std::string &version,
    const std::string &case_id,
    const std::string &input_file_path)
{
    return nfs_mnt_dir / simulator / version / case_id / input_file_path;
}

inline fs::path abs_output_file_path(
    const std::string &simulator,
    const std::string &version,
    const std::string &case_id,
    const std::string &output_file_path)
{
    return nfs_mnt_dir / simulator / version / case_id / output_file_path;
}
