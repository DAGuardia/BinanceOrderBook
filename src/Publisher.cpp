#include "Publisher.h"
#include "Utils.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <iomanip>

Publisher::Publisher(
    std::unordered_map<std::string, std::shared_ptr<OrderBook>> books,
    std::unordered_map<std::string, std::shared_ptr<TradeStats>> trades,
    int topN,
    const std::string& logPath
)
    : _books(std::move(books))
    , _trades(std::move(trades))
    , _topN(topN)
    , _logPath(logPath)
{
}

void Publisher::start() {
    if (!_logPath.empty()) {
        _file.open(_logPath, std::ios::out | std::ios::app);
    }
    _running = true;
    _thr = std::thread(&Publisher::run, this);
}

void Publisher::stop() {
    _running = false;
    if (_thr.joinable()) {
        _thr.join();
    }
    if (_file.is_open()) {
        _file.close();
    }
}

void Publisher::run() {
    using namespace std::chrono_literals;

    while (_running) {

        for (auto& kv : _books) {
            const std::string& sym = kv.first;
            auto& bookPtr = kv.second;

            // Snapshot consistente del libro (topN niveles, best bid/ask, etc.)
            auto snapBook = bookPtr->snapshot(_topN);

            // Snapshot consistente de trade metrics (último trade, VWAP sesión)
            TradeSnapshot snapTrade;
            if (_trades.count(sym)) {
                snapTrade = _trades[sym]->snapshot();
            }

            // mid y spread
            double mid = (snapBook.bestBidPx > 0.0 && snapBook.bestAskPx > 0.0)
                ? (snapBook.bestBidPx + snapBook.bestAskPx) / 2.0
                : 0.0;

            double spread = (snapBook.bestBidPx > 0.0 && snapBook.bestAskPx > 0.0)
                ? (snapBook.bestAskPx - snapBook.bestBidPx)
                : 0.0;

            // imbalance (profundidad relativa de bids vs asks en topN)
            double bidDepthSum = 0.0;
            for (auto& lvl : snapBook.topBids) bidDepthSum += lvl.qty;
            double askDepthSum = 0.0;
            for (auto& lvl : snapBook.topAsks) askDepthSum += lvl.qty;

            double imb = 0.0;
            if (bidDepthSum + askDepthSum > 0.0) {
                imb = bidDepthSum / (bidDepthSum + askDepthSum);
            }

            // timestamp epoch con decimales
            double ts = nowUnixSeconds();

            // helper para serializar niveles: "price:qty|price:qty|..."
            auto vecToStr = [](const std::vector<Level>& v) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(6);
                for (size_t i = 0; i < v.size(); ++i) {
                    oss << v[i].price << ":" << v[i].qty;
                    if (i + 1 < v.size()) oss << "|";
                }
                return oss.str();
                };

            // armar CSV
            std::ostringstream line;
            line << std::fixed << std::setprecision(6);

            line
                << ts << ","
                << sym << ","
                << mid << ","
                << spread << ","
                << snapBook.bestBidPx << ","
                << snapBook.bestBidQty << ","
                << snapBook.bestAskPx << ","
                << snapBook.bestAskQty << ","
                << vecToStr(snapBook.topBids) << ","
                << vecToStr(snapBook.topAsks) << ","
                << snapTrade.last.price << ","
                << snapTrade.last.qty << ","
                << (snapTrade.last.side.empty() ? "none" : snapTrade.last.side) << ","
                << snapTrade.vwapWindow << ","
                << snapTrade.vwapSession << ","
                << imb;

            // validación básica del libro (best_bid < best_ask, etc.)
            if (!bookPtr->isSane()) {
                std::cerr << "[WARN] book inconsistente para " << sym << "\n";
            }

            std::string outLine = line.str();

            if (_file.is_open()) {
                _file << outLine << "\n";
                _file.flush();
            }
            else {
                std::cout << outLine << "\n";
            }
        }
        std::this_thread::sleep_for(1000ms);
    }
}