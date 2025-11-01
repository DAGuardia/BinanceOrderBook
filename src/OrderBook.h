#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <functional>

struct Level {
    double price;
    double qty;
};

struct BookSnapshot {
    std::string symbol;
    double bestBidPx = 0.0;
    double bestBidQty = 0.0;
    double bestAskPx = 0.0;
    double bestAskQty = 0.0;
    std::vector<Level> topBids;
    std::vector<Level> topAsks;
};

struct DepthUpdate {
    uint64_t firstUpdateId; // U
    uint64_t lastUpdateId;  // u
    std::vector<std::pair<double, double>> bids; // price, qty
    std::vector<std::pair<double, double>> asks; // price, qty
};

class OrderBook {
public:
    explicit OrderBook(std::string sym);

    void applyBidLevel(double px, double qty);
    void applyAskLevel(double px, double qty);

    // aplica un update incremental (bids/asks)
    void applyDepthDelta(const DepthUpdate& up);

    BookSnapshot snapshot(int topN);
    bool isSane() const;

private:
    std::string _symbol;

    // price -> qty
    std::map<double, double, std::greater<double>> _bids;
    std::map<double, double, std::less<double>>    _asks;

    mutable std::mutex _mtx;
};