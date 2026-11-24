#include "sdr_stream.hpp"
#include "signal_gen.hpp"
#include <SoapySDR/Formats.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

int run_sdr_streams(SoapySDR::Device *sdr, const AppConfig &config)
{
    std::vector<size_t> channels = {0};

    SoapySDR::Stream *rxStream = nullptr;
    SoapySDR::Stream *txStream = nullptr;

    try {
        rxStream = sdr->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, channels);
    } catch (const std::exception &ex) {
        std::cerr << "setupStream rx fail: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    try {
        txStream = sdr->setupStream(SOAPY_SDR_TX, SOAPY_SDR_CS16, channels);
    } catch (const std::exception &ex) {
        std::cerr << "setupStream tx fail: " << ex.what() << '\n';
        sdr->closeStream(rxStream);
        return EXIT_FAILURE;
    }

    size_t rx_mtu = sdr->getStreamMTU(rxStream);
    size_t tx_mtu = sdr->getStreamMTU(txStream);
    std::cout << "MTU - TX: " << tx_mtu << ", RX: " << rx_mtu << '\n';

    std::vector<int16_t> tx_buffer(2 * tx_mtu);
    std::vector<int16_t> rx_buffer(2 * rx_mtu);

    sdr->activateStream(rxStream);
    sdr->activateStream(txStream);

    std::cout << "Start test...\n";

    const std::string current_file = (config.mode == 0) ? "Txdata.pcm" : "Rxdata.pcm";
    std::ofstream dump_file(current_file, std::ios::binary);
    if (!dump_file.is_open()) {
        std::cerr << "Failed to open " << current_file << '\n';
    }

    for (size_t n = 0; n < WARMUP_BUFFERS; ) {
        void *rx_buffs[] = {rx_buffer.data()};
        int flags = 0;
        long long t = 0;
        int sr = sdr->readStream(rxStream, rx_buffs, rx_mtu, flags, t, TIMEOUT_US);
        if (sr < 0) continue;
        n++;
    }

    long long last_time = 0;
    size_t global_sample_index = 0;

    for (size_t buffers_read = 0; buffers_read < ITERATION_COUNT; ++buffers_read) {
        void *rx_buffs[] = {rx_buffer.data()};
        int flags = 0;
        long long timeNs = 0;

        int sr = sdr->readStream(rxStream, rx_buffs, rx_mtu, flags, timeNs, TIMEOUT_US);
        if (sr < 0) continue;

        std::cout << "Buffer: " << buffers_read
                  << " - Samples: " << sr
                  << ", Flags: " << flags
                  << ", Time: " << timeNs
                  << ", TimeDiff: " << (timeNs - last_time) << '\n';
        last_time = timeNs;

        if (config.mode == 1 && dump_file.is_open()) {
            dump_file.write(reinterpret_cast<const char*>(rx_buffer.data()), sr * 2 * sizeof(int16_t));
        }

        size_t cycle_pos = buffers_read % TOTAL_CYCLE_BUFFERS;
        bool is_burst = (cycle_pos < BURST_LENGTH_BUFFERS);

        signal_gen_fill_tx_buffer(tx_buffer.data(), tx_mtu, global_sample_index, is_burst);

        if (config.mode == 0 && dump_file.is_open()) {
            dump_file.write(reinterpret_cast<const char*>(tx_buffer.data()), tx_mtu * 2 * sizeof(int16_t));
        }

        long long tx_time = timeNs + TX_TIME_OFFSET_NS;

        if (buffers_read > 0) {
            const void *tx_buffs[] = {tx_buffer.data()};
            int tx_flags = SOAPY_SDR_HAS_TIME;
            int st = sdr->writeStream(txStream, tx_buffs, tx_mtu, tx_flags, tx_time, TIMEOUT_US);
            if (static_cast<size_t>(st) != tx_mtu) {
                std::cerr << "TX Failed: " << st << '\n';
            }
        }

        global_sample_index += tx_mtu;
    }

    if (dump_file.is_open()) {
        dump_file.close();
    }

    sdr->deactivateStream(rxStream);
    sdr->deactivateStream(txStream);
    sdr->closeStream(rxStream);
    sdr->closeStream(txStream);

    return EXIT_SUCCESS;
}
