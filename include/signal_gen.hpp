#pragma once

#include "config.hpp"
#include <cstddef>
#include <cstdint>

void fill_tx_buffer(int16_t *tx_buffer, size_t tx_mtu,
                    size_t global_sample_index, bool is_burst);