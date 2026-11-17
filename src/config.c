#include "config.h"
#include <string.h>

void init_config(AppConfig *config, int mode)
{
    config->mode = mode;
    config->rate = 1e6;
    config->tx_gain = -40.0;
    config->rx_gain = 30.0;

    if (mode == 0) {
        config->rx_freq = 750e6;
        config->tx_freq = 800e6;
        strcpy(config->uri, "usb:1.11.5");
    } else {
        config->rx_freq = 800e6;
        config->tx_freq = 750e6;
        strcpy(config->uri, "usb:1.13.5");
    }
}
