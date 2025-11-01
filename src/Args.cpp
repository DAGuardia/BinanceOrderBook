#include "Args.h"
#include "Utils.h"
#include <stdexcept>
#include <cstring>
#include <cctype>

ProgramArgs parseArgs(int argc, char** argv) {
    ProgramArgs args;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];

        if (std::strncmp(a, "--symbols=", 10) == 0) {
            std::string list = a + 10;
            args.symbols = splitCsv(list);
        }
        else if (std::strncmp(a, "--topN=", 7) == 0) {
            args.topN = std::stoi(a + 7);
        }
        else if (std::strncmp(a, "--log=", 6) == 0) {
            args.logPath = a + 6;
        }
        else {
            throw std::runtime_error(std::string("Argumento desconocido: ") + a);
        }
    }

    if (args.symbols.empty()) {
        throw std::runtime_error("Falta --symbols=btcusdt,ethusdt,...");
    }
    if (args.topN <= 0) {
        throw std::runtime_error("--topN debe ser > 0");
    }

    return args;
}