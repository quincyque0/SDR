#include "sdr_device.hpp"
#include <iostream>

DevicePtr create_device(const std::string &uri)
{
    SoapySDR::Kwargs args;
    args["driver"] = "plutosdr";
    args["uri"] = uri;
    args["loopback"] = "0";

    SoapySDR::Device *device = nullptr;
    try {
        device = SoapySDR::Device::make(args);
    } catch (const std::exception &ex) {
        std::cerr << "SoapySDR::Device::make fail: " << ex.what() << '\n';
        return nullptr;
    }

    return DevicePtr(device);
}

void configure_rf(SoapySDR::Device *sdr, const AppConfig &config)
{
    try {
        sdr->setSampleRate(SOAPY_SDR_RX, 0, config.rate);
        sdr->setFrequency(SOAPY_SDR_RX, 0, config.rx_freq);
        sdr->setGain(SOAPY_SDR_RX, 0, config.rx_gain);

        sdr->setSampleRate(SOAPY_SDR_TX, 0, config.rate);
        sdr->setFrequency(SOAPY_SDR_TX, 0, config.tx_freq);
        sdr->setGain(SOAPY_SDR_TX, 0, config.tx_gain);

        std::cout << "TX carrier: " << (sdr->getFrequency(SOAPY_SDR_TX, 0) / 1e6) << " MHz\n";
        std::cout << "RX carrier: " << (sdr->getFrequency(SOAPY_SDR_RX, 0) / 1e6) << " MHz\n";
    } catch (const std::exception &ex) {
        std::cerr << "configure_rf fail: " << ex.what() << '\n';
    }
}
