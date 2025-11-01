#pragma once
#include <chrono>
#include <string>
#include <vector>

inline double nowUnixSeconds() {
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    auto us = duration_cast<microseconds>(now).count();
    return us / 1'000'000.0;
}

inline std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::string curr;
    for (char c : s) {
        if (c == ',') {
            if (!curr.empty()) out.push_back(curr);
            curr.clear();
        }
        else {
            curr.push_back(c);
        }
    }
    if (!curr.empty()) out.push_back(curr);
    return out;
}