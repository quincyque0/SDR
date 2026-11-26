#pragma once

#include <cstddef>
#include <string>

constexpr int AMPLITUDE = 2000;
constexpr int PERIOD = 1920;
constexpr int HALF_PERIOD = PERIOD / 2;
constexpr int BURST_LENGTH_BUFFERS = 10;
constexpr int GAP_LENGTH_BUFFERS = 10;
constexpr int TOTAL_CYCLE_BUFFERS = BURST_LENGTH_BUFFERS + GAP_LENGTH_BUFFERS;
constexpr size_t WARMUP_BUFFERS = 16;
constexpr size_t ITERATION_COUNT = 500;
constexpr int TIMEOUT_US = 400000;
constexpr long long TX_TIME_OFFSET_NS = 4000000LL;

struct AppConfig {
  int mode = 0;
  std::string uri;
  double tx_freq = 0.0;
  double rx_freq = 0.0;
  double rate = 0.0;
  double tx_gain = 0.0;
  double rx_gain = 0.0;
};

void init_config(AppConfig &config, int mode);