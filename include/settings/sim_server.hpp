#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

inline const std::string request_manager_ip = "10.10.10.250";
inline const std::string request_manager_port = "8000";
inline const std::string request_manager_target = "/ndt/simulation_completed";

// inline const std::string sim_server_ip = "127.0.0.1";
inline const uint32_t sim_server_port = 9000;
inline const std::string sim_server_target = "/submit";

inline const std::string nfs_server_ip = "10.10.10.250";
inline const std::string nfs_server_dir = "/srv/nfs/sim";
inline const fs::path nfs_mnt_dir = "/mnt/nfs/sim";

inline const fs::path registered_dir = "registered/";
inline const fs::path simulator_executable = "executable";

// In the future, the decision may be based on the settings in all_simulators.json, and the parameter of simulator_exec_command will no longer be outputpath, but outputdir.
inline const fs::path output_filename = "output";

inline std::string mount_nfs_command()
{
    return "mount -t nfs " + nfs_server_ip + ":" + nfs_server_dir + " " + nfs_mnt_dir.string();
}

inline std::string unmount_nfs_command()
{
    return "umount " + nfs_mnt_dir.string();
}

inline fs::path abs_input_file_path(
    const std::string &simulator,
    const std::string &version,
    const std::string &app_id,
    const std::string &case_id,
    const std::string &input_file_path)
{
    return nfs_mnt_dir / app_id / simulator / version / case_id / input_file_path;
}

inline fs::path abs_output_file_path(
    const std::string &simulator,
    const std::string &version,
    const std::string &app_id,
    const std::string &case_id)
{
    return nfs_mnt_dir / app_id / simulator / version / case_id / output_filename;
}

inline bool check_simulator_exist(const std::string &simulator, const std::string &version)
{
    return fs::exists(registered_dir / simulator / version / simulator_executable);
}

inline std::string simulator_exec_command(
    const std::string &simulator,
    const std::string &version,
    const std::string &abs_input_file_path,
    const std::string &abs_output_file_path)
{
    fs::path simulator_exec = registered_dir / simulator / version / simulator_executable;
    return simulator_exec.string() + " " + abs_input_file_path + " " + abs_output_file_path;
}
