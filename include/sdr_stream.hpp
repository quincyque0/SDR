#pragma once

#include "config.hpp"
#include <SoapySDR/Device.hpp>

int run_sdr_streams(SoapySDR::Device *sdr, const AppConfig &config);
