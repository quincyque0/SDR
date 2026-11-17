#ifndef SIGNAL_GEN_H
#define SIGNAL_GEN_H

#include "config.h"
#include <stdint.h>
#include <stddef.h>

void fill_tx_buffer(int16_t *tx_buffer, size_t tx_mtu, size_t global_sample_index, int is_burst);

#endif
