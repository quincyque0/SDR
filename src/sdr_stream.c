#include "sdr_stream.h"
#include "signal_gen.h"
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static void cleanup_streams(SoapySDRDevice *sdr, SoapySDRStream *rx_stream, SoapySDRStream *tx_stream,
                            int16_t *tx_buffer, int16_t *rx_buffer)
{
    if (rx_stream) {
        SoapySDRDevice_deactivateStream(sdr, rx_stream, 0, 0);
        SoapySDRDevice_closeStream(sdr, rx_stream);
    }
    if (tx_stream) {
        SoapySDRDevice_deactivateStream(sdr, tx_stream, 0, 0);
        SoapySDRDevice_closeStream(sdr, tx_stream);
    }
    free(tx_buffer);
    free(rx_buffer);
}

int run_sdr_streams(SoapySDRDevice *sdr, const AppConfig *config)
{
    size_t channels[] = {0};
    size_t channel_count = sizeof(channels) / sizeof(channels[0]);

    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (!rxStream) {
        printf("setupStream rx fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    SoapySDRStream *txStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (!txStream) {
        printf("setupStream tx fail: %s\n", SoapySDRDevice_lastError());
        cleanup_streams(sdr, rxStream, NULL, NULL, NULL);
        return EXIT_FAILURE;
    }

    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
    printf("MTU - TX: %zu, RX: %zu\n", tx_mtu, rx_mtu);

    int16_t *tx_buffer = malloc(2 * tx_mtu * sizeof(int16_t));
    int16_t *rx_buffer = malloc(2 * rx_mtu * sizeof(int16_t));
    if (!tx_buffer || !rx_buffer) {
        perror("malloc");
        cleanup_streams(sdr, rxStream, txStream, tx_buffer, rx_buffer);
        return EXIT_FAILURE;
    }

    if (SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0) != 0)
        printf("activateStream rx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0) != 0)
        printf("activateStream tx fail: %s\n", SoapySDRDevice_lastError());

    printf("Start test...\n");

    const char *current_file = (config->mode == 0) ? "Txdata.pcm" : "Rxdata.pcm";
    FILE *dump_file = fopen(current_file, "wb");
    if (!dump_file)
        perror("fopen");

    for (size_t n = 0; n < WARMUP_BUFFERS; ) {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;
        long long t = 0;
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &t, TIMEOUT_US);
        if (sr < 0)
            continue;
        n++;
    }

    long long last_time = 0;
    size_t global_sample_index = 0;

    for (size_t buffers_read = 0; buffers_read < ITERATION_COUNT; buffers_read++) {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;
        long long timeNs = 0;

        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, TIMEOUT_US);
        if (sr < 0)
            continue;

        printf("Buffer: %zu - Samples: %i, Flags: %i, Time: %lld, TimeDiff: %lld\n",
               buffers_read, sr, flags, timeNs, timeNs - last_time);
        last_time = timeNs;

        if (config->mode == 1 && dump_file)
            fwrite(rx_buffer, sizeof(int16_t) * 2, (size_t)sr, dump_file);

        size_t cycle_pos = buffers_read % TOTAL_CYCLE_BUFFERS;
        int is_burst = (cycle_pos < BURST_LENGTH_BUFFERS);

        fill_tx_buffer(tx_buffer, tx_mtu, global_sample_index, is_burst);

        if (config->mode == 0 && dump_file)
            fwrite(tx_buffer, sizeof(int16_t) * 2, tx_mtu, dump_file);

        long long tx_time = timeNs + TX_TIME_OFFSET_NS;

        if (buffers_read > 0) {
            void *tx_buffs[] = {tx_buffer};
            int tx_flags = SOAPY_SDR_HAS_TIME;
            int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const *)tx_buffs,
                                                tx_mtu, &tx_flags, tx_time, TIMEOUT_US);
            if ((size_t)st != tx_mtu)
                printf("TX Failed: %i (%s)\n", st, SoapySDRDevice_lastError());
        }

        global_sample_index += tx_mtu;
    }

    if (dump_file)
        fclose(dump_file);

    cleanup_streams(sdr, rxStream, txStream, tx_buffer, rx_buffer);
    return EXIT_SUCCESS;
}
