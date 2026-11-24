#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

constexpr int16_t AMPLITUDE = 2000;
constexpr size_t PERIOD = 1920;
constexpr size_t HALF_PERIOD = PERIOD / 2;
constexpr size_t BURST_LENGTH_BUFFERS = 10;
constexpr size_t GAP_LENGTH_BUFFERS = 10;
constexpr size_t TOTAL_CYCLE_BUFFERS = BURST_LENGTH_BUFFERS + GAP_LENGTH_BUFFERS;
constexpr size_t WARMUP_BUFFERS = 16;
constexpr size_t ITERATION_COUNT = 500;
constexpr long TIMEOUT_US = 400000;
constexpr long long TX_TIME_OFFSET_NS = 4000000LL;

struct AppConfig {
    int mode{0};
    std::string uri;
    double tx_freq{800e6};
    double rx_freq{750e6};
    double rate{1e6};
    double tx_gain{-40.0};
    double rx_gain{30.0};
};

AppConfig init_config(int mode);
