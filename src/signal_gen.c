#include "signal_gen.h"
#include <string.h>

void fill_tx_buffer(int16_t *tx_buffer, size_t tx_mtu, size_t global_sample_index, int is_burst)
{
    if (!is_burst) {
        memset(tx_buffer, 0, 2 * tx_mtu * sizeof(int16_t));
        return;
    }

    for (size_t n = 0; n < tx_mtu; ++n) {
        size_t pos = global_sample_index + n;
        int16_t sample_i = (pos < HALF_PERIOD) ? AMPLITUDE : 0;
        int16_t sample_q = (pos < HALF_PERIOD) ? 0 : AMPLITUDE;

        tx_buffer[2 * n] = sample_i;
        tx_buffer[2 * n + 1] = sample_q;
    }
}
