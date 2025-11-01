#pragma once
#include <string>
#include <vector>

struct ProgramArgs {
    std::vector<std::string> symbols;
    int topN = 5;
    std::string logPath;
};


ProgramArgs parseArgs(int argc, char** argv);