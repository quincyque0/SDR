#include "sdr_stream.hpp"
#include "signal_gen.hpp"

#include <SoapySDR/Formats.h>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

void handle_signal(int) {
  g_running = false;
}

struct StreamGuard {
  SoapySDRDevice *sdr = nullptr;
  SoapySDRStream *rx = nullptr;
  SoapySDRStream *tx = nullptr;
  std::FILE *dump = nullptr;

  ~StreamGuard() {
    if (rx) {
      SoapySDRDevice_deactivateStream(sdr, rx, 0, 0);
      SoapySDRDevice_closeStream(sdr, rx);
    }
    if (tx) {
      SoapySDRDevice_deactivateStream(sdr, tx, 0, 0);
      SoapySDRDevice_closeStream(sdr, tx);
    }
    if (dump) {
      std::fclose(dump);
      dump = nullptr;
    }
  }
};

} // namespace

int run_sdr_streams(SoapySDRDevice *sdr, const AppConfig &config) {
  g_running = true;
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  size_t channels[] = {0};
  size_t channel_count = sizeof(channels) / sizeof(channels[0]);

  StreamGuard guard;
  guard.sdr = sdr;

  guard.rx = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16,
                                        channels, channel_count, nullptr);
  if (!guard.rx) {
    std::printf("setupStream rx fail: %s\n", SoapySDRDevice_lastError());
    return EXIT_FAILURE;
  }

  guard.tx = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16,
                                        channels, channel_count, nullptr);
  if (!guard.tx) {
    std::printf("setupStream tx fail: %s\n", SoapySDRDevice_lastError());
    return EXIT_FAILURE;
  }

  size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, guard.rx);
  size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, guard.tx);
  std::printf("MTU - TX: %zu, RX: %zu\n", tx_mtu, rx_mtu);

  std::vector<int16_t> tx_buffer(2 * tx_mtu);
  std::vector<int16_t> rx_buffer(2 * rx_mtu);

  if (SoapySDRDevice_activateStream(sdr, guard.rx, 0, 0, 0) != 0)
    std::printf("activateStream rx fail: %s\n", SoapySDRDevice_lastError());
  if (SoapySDRDevice_activateStream(sdr, guard.tx, 0, 0, 0) != 0)
    std::printf("activateStream tx fail: %s\n", SoapySDRDevice_lastError());

  std::printf("Start continuous streaming (Ctrl+C to stop)...\n");

  const char *current_file = (config.mode == 0) ? "Txdata.pcm" : "Rxdata.pcm";
  guard.dump = std::fopen(current_file, "wb");
  if (!guard.dump)
    std::perror("fopen");

  for (size_t n = 0; n < WARMUP_BUFFERS && g_running;) {
    void *rx_buffs[] = {rx_buffer.data()};
    int flags = 0;
    long long t = 0;
    int sr = SoapySDRDevice_readStream(sdr, guard.rx, rx_buffs, rx_mtu, &flags,
                                       &t, TIMEOUT_US);
    if (sr < 0)
      continue;
    n++;
  }

  long long last_time = 0;
  size_t global_sample_index = 0;
  size_t buffers_read = 0;
  bool dump_finished = false;

  while (g_running) {
    void *rx_buffs[] = {rx_buffer.data()};
    int flags = 0;
    long long timeNs = 0;

    int sr = SoapySDRDevice_readStream(sdr, guard.rx, rx_buffs, rx_mtu, &flags,
                                       &timeNs, TIMEOUT_US);
    if (sr < 0)
      continue;

    if (buffers_read < 20 || buffers_read % 100 == 0) {
      std::printf(
          "Buffer: %zu - Samples: %i, Flags: %i, Time: %lld, TimeDiff: %lld\n",
          buffers_read, sr, flags, timeNs, timeNs - last_time);
    }
    last_time = timeNs;

    if (config.mode == 1 && guard.dump && !dump_finished) {
      std::fwrite(rx_buffer.data(), sizeof(int16_t) * 2,
                  static_cast<size_t>(sr), guard.dump);
    }

    bool is_burst = true;
    fill_tx_buffer(tx_buffer.data(), tx_mtu, global_sample_index, is_burst);

    if (config.mode == 0 && guard.dump && !dump_finished) {
      std::fwrite(tx_buffer.data(), sizeof(int16_t) * 2, tx_mtu, guard.dump);
    }

    if (!dump_finished && buffers_read >= ITERATION_COUNT) {
      dump_finished = true;
      if (guard.dump) {
        std::fflush(guard.dump);
        std::fclose(guard.dump);
        guard.dump = nullptr;
        std::printf("Saved %zu buffers to %s. RX and TX streams running continuously...\n",
                    ITERATION_COUNT, current_file);
      }
    }

    long long tx_time = timeNs + TX_TIME_OFFSET_NS;

    if (buffers_read > 0) {
      void *tx_buffs[] = {tx_buffer.data()};
      int tx_flags = SOAPY_SDR_HAS_TIME;
      int st = SoapySDRDevice_writeStream(sdr, guard.tx,
                                          (const void *const *)tx_buffs, tx_mtu,
                                          &tx_flags, tx_time, TIMEOUT_US);
      if (static_cast<size_t>(st) != tx_mtu)
        std::printf("TX Failed: %i (%s)\n", st, SoapySDRDevice_lastError());
    }

    global_sample_index += tx_mtu;
    buffers_read++;
  }

  std::printf("\nStream stopped gracefully. Total buffers processed: %zu\n", buffers_read);
  return EXIT_SUCCESS;
}
