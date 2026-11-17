#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define AMPLITUDE 2000
#define PERIOD 1920
#define HALF_PERIOD (PERIOD / 2)
#define BURST_LENGTH_BUFFERS 10
#define GAP_LENGTH_BUFFERS 10
#define TOTAL_CYCLE_BUFFERS (BURST_LENGTH_BUFFERS + GAP_LENGTH_BUFFERS)
#define WARMUP_BUFFERS 16
#define ITERATION_COUNT 500
#define TIMEOUT_US 400000
#define TX_TIME_OFFSET_NS 4000000LL

typedef struct {
    int mode;
    char uri[128];
    double tx_freq;
    double rx_freq;
    double rate;
    double tx_gain;
    double rx_gain;
} AppConfig;

void init_config(AppConfig *config, int mode);

#endif
