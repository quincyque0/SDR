#ifndef SDR_DEVICE_H
#define SDR_DEVICE_H

#include "config.h"
#include <SoapySDR/Device.h>

SoapySDRDevice *create_device(const char *uri);
void configure_rf(SoapySDRDevice *sdr, const AppConfig *config);
void destroy_device(SoapySDRDevice *sdr);

#endif
