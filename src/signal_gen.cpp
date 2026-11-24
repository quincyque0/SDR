#include "signal_gen.hpp"
#include <cstdlib>
#include <cstring>

std::vector<uint8_t> generate_random_bits(size_t num_bits)
{
    std::vector<uint8_t> bits(num_bits);
    for (size_t i = 0; i < num_bits; ++i) {
        bits[i] = static_cast<uint8_t>(rand() & 1);
    }
    return bits;
}

std::vector<ComplexSample> modulate_bpsk(const std::vector<uint8_t> &bits, int16_t amplitude)
{
    std::vector<ComplexSample> symbols(bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        int16_t val_i = bits[i] ? amplitude : static_cast<int16_t>(-amplitude);
        symbols[i] = ComplexSample(val_i, 0);
    }
    return symbols;
}

std::vector<ComplexSample> modulate_qpsk(const std::vector<uint8_t> &bits, int16_t amplitude)
{
    size_t num_symbols = bits.size() / 2;
    std::vector<ComplexSample> symbols(num_symbols);
    for (size_t i = 0; i < num_symbols; ++i) {
        int16_t val_i = bits[2 * i] ? amplitude : static_cast<int16_t>(-amplitude);
        int16_t val_q = bits[2 * i + 1] ? amplitude : static_cast<int16_t>(-amplitude);
        symbols[i] = ComplexSample(val_i, val_q);
    }
    return symbols;
}

std::vector<ComplexSample> upsample_symbols(const std::vector<ComplexSample> &symbols, size_t sps)
{
    std::vector<ComplexSample> upsampled(symbols.size() * sps, ComplexSample(0, 0));
    for (size_t i = 0; i < symbols.size(); ++i) {
        upsampled[i * sps] = symbols[i];
    }
    return upsampled;
}

std::vector<ComplexSample> convolve_rectangular(const std::vector<ComplexSample> &upsampled, size_t sps)
{
    size_t n_samples = upsampled.size();
    std::vector<ComplexSample> shaped(n_samples);

    for (size_t n = 0; n < n_samples; ++n) {
        int32_t acc_i = 0;
        int32_t acc_q = 0;

        for (size_t k = 0; k < sps; ++k) {
            if (n >= k) {
                acc_i += upsampled[n - k].real();
                acc_q += upsampled[n - k].imag();
            }
        }

        shaped[n] = ComplexSample(static_cast<int16_t>(acc_i), static_cast<int16_t>(acc_q));
    }

    return shaped;
}

void signal_gen_fill_tx_buffer(int16_t *tx_buffer, size_t tx_mtu, size_t global_sample_index, bool is_burst)
{
    if (!is_burst) {
        std::memset(tx_buffer, 0, 2 * tx_mtu * sizeof(int16_t));
        return;
    }

    constexpr size_t sps = 16;
    size_t num_symbols = (tx_mtu + sps - 1) / sps;
    size_t num_bits = num_symbols * 2;

    srand(static_cast<unsigned int>(global_sample_index + 1));
    std::vector<uint8_t> bits = generate_random_bits(num_bits);
    std::vector<ComplexSample> symbols = modulate_qpsk(bits, AMPLITUDE);
    std::vector<ComplexSample> upsampled = upsample_symbols(symbols, sps);
    std::vector<ComplexSample> shaped = convolve_rectangular(upsampled, sps);

    for (size_t n = 0; n < tx_mtu; ++n) {
        tx_buffer[2 * n] = shaped[n].real();
        tx_buffer[2 * n + 1] = shaped[n].imag();
    }
}
