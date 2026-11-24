#include "config.hpp"
#include "sdr_device.hpp"
#include "sdr_stream.hpp"
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cout << "Argument required: 1 - Rx, 0 - Tx\n";
        return EXIT_FAILURE;
    }

    int mode = std::stoi(argv[1]);
    AppConfig config = init_config(mode);

    DevicePtr sdr = create_device(config.uri);
    if (!sdr) {
        return EXIT_FAILURE;
    }

    configure_rf(sdr.get(), config);

    int status = run_sdr_streams(sdr.get(), config);

    if (status == EXIT_SUCCESS) {
        std::cout << "test complete!\n";
    }

    return status;
}
