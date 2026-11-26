#pragma once

#include "config.hpp"
#include <SoapySDR/Device.h>

SoapySDRDevice *create_device(const std::string &uri);
void configure_rf(SoapySDRDevice *sdr, const AppConfig &config);
void destroy_device(SoapySDRDevice *sdr);