#pragma once

#include "config.hpp"
#include <SoapySDR/Device.hpp>
#include <memory>
#include <string>

struct SoapyDeviceDeleter {
    void operator()(SoapySDR::Device *device) const {
        if (device) {
            SoapySDR::Device::unmake(device);
        }
    }
};

using DevicePtr = std::unique_ptr<SoapySDR::Device, SoapyDeviceDeleter>;

DevicePtr create_device(const std::string &uri);
void configure_rf(SoapySDR::Device *sdr, const AppConfig &config);
