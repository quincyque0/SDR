#ifndef SDR_STREAM_H
#define SDR_STREAM_H

#include "config.h"
#include <SoapySDR/Device.h>

int run_sdr_streams(SoapySDRDevice *sdr, const AppConfig *config);

#endif
