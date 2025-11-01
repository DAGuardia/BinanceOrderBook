#pragma once
#include <string>
#include <vector>

struct ProgramArgs {
    std::vector<std::string> symbols;
    int topN = 5;
    std::string logPath; // vacío = stdout
};

ProgramArgs parseArgs(int argc, char** argv);