/* Command-line and host runtime configuration. */
#ifndef GOF2HD_CONFIG_H
#define GOF2HD_CONFIG_H

typedef struct HostConfig {
    int valid;
    const char* program_name;
    const char* apk_path;
    const char* obb_path;
    const char* data_dir;
    int verbose_jni;
    int disable_crash_handler;
    int gyro_mode;
} HostConfig;

/* Pointers in the result refer to argv and remain valid for main's lifetime. */
HostConfig config_parse(int argc, char** argv);
void config_print_usage(const HostConfig* config);

#endif
