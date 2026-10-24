#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>
#include <string.h>

typedef struct {
    int    mode;
    char   uri[128];
    double tx_freq;
    double rx_freq;
    double rate;
    double tx_gain;
    double rx_gain;
} AppConfig;

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Argument required: 1 - Rx, 0 - Tx\n");
        return EXIT_FAILURE;
    }

    AppConfig Config;
    if (atoi(argv[1]) == 0) {
        Config.mode    = 0;
        Config.rx_freq = 750e6;
        Config.tx_freq = 800e6;
        Config.rate    = 1e6;
        Config.tx_gain = -40.;
        Config.rx_gain = 30.;
        strcpy(Config.uri, "usb:1.11.5");
    } else {
        Config.mode    = 1;
        Config.rx_freq = 800e6;
        Config.tx_freq = 750e6;
        Config.rate    = 1e6;
        Config.tx_gain = -40.;
        Config.rx_gain = 30.;
        strcpy(Config.uri, "usb:1.13.5");
    }

    SoapySDRKwargs args = {0};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", Config.uri);
    SoapySDRKwargs_set(&args, "loopback", "0");

    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (sdr == NULL) {
        printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, Config.rate) != 0)
        printf("setSampleRate rx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, Config.rx_freq, NULL) != 0)
        printf("setFrequency rx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, Config.rx_gain) != 0)
        printf("setGain rx fail: %s\n", SoapySDRDevice_lastError());

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, Config.rate) != 0)
        printf("setSampleRate tx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, Config.tx_freq, NULL) != 0)
        printf("setFrequency tx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, Config.tx_gain) != 0)
        printf("setGain tx fail: %s\n", SoapySDRDevice_lastError());

    printf("TX carrier: %.3f MHz\n", SoapySDRDevice_getFrequency(sdr, SOAPY_SDR_TX, 0) / 1e6);
    printf("RX carrier: %.3f MHz\n", SoapySDRDevice_getFrequency(sdr, SOAPY_SDR_RX, 0) / 1e6);

    size_t channels[] = {0};
    size_t channel_count = sizeof(channels) / sizeof(channels[0]);

    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (rxStream == NULL) {
        printf("setupStream rx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    SoapySDRStream *txStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (txStream == NULL) {
        printf("setupStream tx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
    printf("MTU - TX: %zu, RX: %zu\n", tx_mtu, rx_mtu);

    int16_t *tx_buffer = malloc(2 * tx_mtu * sizeof(int16_t));
    int16_t *rx_buffer = malloc(2 * rx_mtu * sizeof(int16_t));
    if (!tx_buffer || !rx_buffer) {
        perror("malloc");
        free(tx_buffer);
        free(rx_buffer);
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    const int16_t AMPLITUDE = 2000;
    const size_t PERIOD      = 1920; 
    const size_t HALF_PERIOD = PERIOD / 2;
    
    size_t global_sample_index = 0;

    const size_t BURST_LENGTH_BUFFERS = 10; 
    const size_t GAP_LENGTH_BUFFERS   = 10; 
    const size_t TOTAL_CYCLE_BUFFERS  = BURST_LENGTH_BUFFERS + GAP_LENGTH_BUFFERS;

    if (SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0) != 0)
        printf("activateStream rx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0) != 0)
        printf("activateStream tx fail: %s\n", SoapySDRDevice_lastError());

    printf("Start test...\n");
    const long timeoutUs = 400000;

    const char *current_file = (Config.mode == 0) ? "Txdata.pcm" : "Rxdata.pcm";
    FILE *dump_file = fopen(current_file, "wb");
    if (!dump_file) perror("fopen");

    long long last_time = 0;
    size_t iteration_count = 500;

    for (size_t n = 0; n < 16; ) {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;
        long long t = 0;
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &t, timeoutUs);
        if (sr < 0) continue;
        n++;
    }

    for (size_t buffers_read = 0; buffers_read < iteration_count; buffers_read++) {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;
        long long timeNs = 0;

        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        if (sr < 0) continue;

        printf("Buffer: %zu - Samples: %i, Flags: %i, Time: %lld, TimeDiff: %lld\n",
               buffers_read, sr, flags, timeNs, timeNs - last_time);
        last_time = timeNs;

        if (Config.mode == 1 && dump_file)
            fwrite(rx_buffer, sizeof(int16_t) * 2, (size_t)sr, dump_file);

        size_t cycle_pos = buffers_read % TOTAL_CYCLE_BUFFERS;
        int is_burst = (cycle_pos < BURST_LENGTH_BUFFERS);

        if (is_burst) {
            for (size_t n = 0; n < tx_mtu; ++n) {
                size_t pos = (global_sample_index + n);
                int16_t sample_i, sample_q;

                if (pos < HALF_PERIOD) {
                    sample_i = AMPLITUDE;
                    sample_q = 0;
                } else {
                    sample_i = 0;
                    sample_q = AMPLITUDE;
                }

                tx_buffer[2 * n]     = sample_i;
                tx_buffer[2 * n + 1] = sample_q;
            }
        } else {
            memset(tx_buffer, 0, 2 * tx_mtu * sizeof(int16_t));
        }

        if (Config.mode == 0 && dump_file)
            fwrite(tx_buffer, sizeof(int16_t) * 2, tx_mtu, dump_file);

        long long tx_time = timeNs + 4 * 1000 * 1000LL;

        if (buffers_read > 0) {
            void *tx_buffs[] = {tx_buffer};
            int tx_flags = SOAPY_SDR_HAS_TIME;

            int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const *)tx_buffs,
                                                tx_mtu, &tx_flags, tx_time, timeoutUs);
            if ((size_t)st != tx_mtu)
                printf("TX Failed: %i (%s)\n", st, SoapySDRDevice_lastError());
        }

        global_sample_index += tx_mtu;
    }

    if (dump_file) fclose(dump_file);

    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);
    SoapySDRDevice_unmake(sdr);

    free(tx_buffer);
    free(rx_buffer);

    printf("test complete!\n");
    return EXIT_SUCCESS;
}
