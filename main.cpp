#include "config.hpp"
#include "sdr_device.hpp"
#include "sdr_stream.hpp"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("Argument required: 1 - Rx, 0 - Tx\n");
    return EXIT_FAILURE;
  }

  AppConfig config;
  init_config(config, std::atoi(argv[1]));

  SoapySDRDevice *sdr = create_device(config.uri);
  if (!sdr) {
    std::printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
    return EXIT_FAILURE;
  }

  configure_rf(sdr, config);

  int status = run_sdr_streams(sdr, config);

  destroy_device(sdr);

  if (status == EXIT_SUCCESS) {
    std::printf("test complete!\n");
  }

  return status;
}