#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include "utils/Logger.hpp"

int main(int argc, char *argv[])
{
    // Number of parameters to check
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <inputfilepath> <outputfilepath>\n";
        return 1;
    }

    auto cfg = Logger::parse_cli_args(argc, argv);
    Logger::init(cfg);
    // SPDLOG_LOGGER_INFO(Logger::instance(), "Logger Loads Successfully!");

    std::string inputFilePath = argv[1];
    std::string outputFilePath = argv[2];

    // Open input file
    std::ifstream inputFile(inputFilePath);
    if (!inputFile.is_open())
    {
        SPDLOG_LOGGER_ERROR(Logger::instance(), "Unable to open input file: {}", inputFilePath);
        return EXIT_FAILURE;
    }

    // Read two numbers
    int a, b;
    if (!(inputFile >> a >> b))
    {
        SPDLOG_LOGGER_ERROR(Logger::instance(),
            "The entered file content is incorrect; it must contain two numbers. File path: '{}'", inputFilePath);

        inputFile.clear();                 // clear failbit
        inputFile.seekg(0);                // rewind
        std::string preview((std::istreambuf_iterator<char>(inputFile)), {});
        if (preview.size() > 200) { preview.resize(200); preview += "…"; }
        SPDLOG_LOGGER_ERROR(Logger::instance(), "File content preview:\n{}", preview);

        return EXIT_FAILURE;
    }


    // std::this_thread::sleep_for(std::chrono::seconds(2));

    int sum = a + b;

    // Write to output file
    std::ofstream outputFile(outputFilePath);
    if (!outputFile.is_open())
    {
        SPDLOG_LOGGER_ERROR(Logger::instance(), "Unable to open output file: {}", outputFilePath);
        return EXIT_FAILURE;
    }

    outputFile << sum << "\n";
    outputFile.close();

    SPDLOG_LOGGER_INFO(Logger::instance(), "Successfully wrote {} + {} = {} into {}", a, b, sum, outputFilePath);
    return EXIT_SUCCESS;
}
