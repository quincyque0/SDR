#include "config.h"
#include "sdr_device.h"
#include "sdr_stream.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Argument required: 1 - Rx, 0 - Tx\n");
        return EXIT_FAILURE;
    }

    AppConfig config;
    init_config(&config, atoi(argv[1]));

    SoapySDRDevice *sdr = create_device(config.uri);
    if (!sdr) {
        printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    configure_rf(sdr, &config);

    int status = run_sdr_streams(sdr, &config);

    destroy_device(sdr);

    if (status == EXIT_SUCCESS) {
        printf("test complete!\n");
    }

    return status;
}
