#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <memory>
#include <fstream>

#include "OrderBook.h"
#include "TradeStats.h"

class Publisher {
public:
    Publisher(std::unordered_map<std::string, std::shared_ptr<OrderBook>> books,
        std::unordered_map<std::string, std::shared_ptr<TradeStats>> trades,
        int topN,
        const std::string& logPath);

    void start();
    void stop();

private:
    void run();

    std::unordered_map<std::string, std::shared_ptr<OrderBook>> _books;
    std::unordered_map<std::string, std::shared_ptr<TradeStats>> _trades;
    int _topN;
    std::string _logPath;

    std::atomic<bool> _running{ false };
    std::thread _thr;
    std::ofstream _file;
};