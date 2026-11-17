#include "sdr_device.h"
#include <stdio.h>

SoapySDRDevice *create_device(const char *uri)
{
    SoapySDRKwargs args = {0};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", uri);
    SoapySDRKwargs_set(&args, "loopback", "0");

    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);
    return sdr;
}

void configure_rf(SoapySDRDevice *sdr, const AppConfig *config)
{
    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, config->rate) != 0)
        printf("setSampleRate rx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, config->rx_freq, NULL) != 0)
        printf("setFrequency rx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, config->rx_gain) != 0)
        printf("setGain rx fail: %s\n", SoapySDRDevice_lastError());

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, config->rate) != 0)
        printf("setSampleRate tx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, config->tx_freq, NULL) != 0)
        printf("setFrequency tx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, config->tx_gain) != 0)
        printf("setGain tx fail: %s\n", SoapySDRDevice_lastError());

    printf("TX carrier: %.3f MHz\n", SoapySDRDevice_getFrequency(sdr, SOAPY_SDR_TX, 0) / 1e6);
    printf("RX carrier: %.3f MHz\n", SoapySDRDevice_getFrequency(sdr, SOAPY_SDR_RX, 0) / 1e6);
}

void destroy_device(SoapySDRDevice *sdr)
{
    if (sdr) {
        SoapySDRDevice_unmake(sdr);
    }
}
