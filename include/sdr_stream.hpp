#pragma once

#include "config.hpp"
#include <SoapySDR/Device.h>

int run_sdr_streams(SoapySDRDevice *sdr, const AppConfig &config);