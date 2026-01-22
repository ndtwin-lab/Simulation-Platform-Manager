#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "settings/sim_server.hpp"

using json = nlohmann::json;

struct SimulationTask
{
    std::string simulator;
    std::string version;
    std::string app_id;
    std::string case_id;
    std::string inputfile;
    std::string outputfile;
};

struct SimulationResult
{
    std::string simulator;
    std::string version;
    std::string app_id;
    std::string case_id;
    std::string outputfile;
    bool success;
};

void from_json(const json &j, SimulationTask &task)
{
    j.at("simulator").get_to(task.simulator);
    j.at("version")  .get_to(task.version);
    j.at("app_id")   .get_to(task.app_id);
    j.at("case_id")  .get_to(task.case_id);
    j.at("inputfile").get_to(task.inputfile);
    task.inputfile  = abs_input_file_path(task.simulator, task.version, task.app_id, task.case_id, task.inputfile);
    task.outputfile = abs_output_file_path(task.simulator, task.version, task.app_id, task.case_id);
}

void to_json(json &j, const SimulationResult &result)
{
    j = json{
        {"simulator" , result.simulator},
        {"version"   , result.version},
        {"app_id"    , result.app_id},
        {"case_id"   , result.case_id},
        {"outputfile", result.outputfile},
        {"success"   , result.success}
    };
}
