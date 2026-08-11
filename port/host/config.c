/* config.c — command-line and host environment parsing. */
#include "config.h"
#include "host_config.h"

#include <stdio.h>
#include <stdlib.h>

HostConfig config_parse(int argc, char** argv) {
    HostConfig config = {0};

    config.program_name = (argc > 0 && argv && argv[0])
        ? argv[0]
        : "gof2hd";
    if (!argv || argc < 4)
        return config;

    config.apk_path = argv[1];
    config.obb_path = argv[2];
    config.data_dir = argv[3];
    config.verbose_jni = getenv(GOF_ENV_VERBOSE_JNI) != NULL;
    config.disable_crash_handler = getenv(GOF_ENV_GDB) != NULL;
    config.gyro_mode = getenv(GOF_ENV_GYRO) != NULL;
    config.valid = 1;
    return config;
}

void config_print_usage(const HostConfig* config) {
    const char* program_name = config && config->program_name
        ? config->program_name
        : "gof2hd";
    fprintf(stderr,
            "usage: %s <base.apk> <main.*.obb> <dataDir>\n",
            program_name);
}
