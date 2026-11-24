#pragma once

#include "config.hpp"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

enum class ModulationType {
    BPSK,
    QPSK
};

using ComplexSample = std::complex<int16_t>;

std::vector<uint8_t> generate_random_bits(size_t num_bits);
std::vector<ComplexSample> modulate_bpsk(const std::vector<uint8_t> &bits, int16_t amplitude);
std::vector<ComplexSample> modulate_qpsk(const std::vector<uint8_t> &bits, int16_t amplitude);
std::vector<ComplexSample> upsample_symbols(const std::vector<ComplexSample> &symbols, size_t sps);
std::vector<ComplexSample> convolve_rectangular(const std::vector<ComplexSample> &upsampled, size_t sps);

void signal_gen_fill_tx_buffer(int16_t *tx_buffer, size_t tx_mtu, size_t global_sample_index, bool is_burst);
