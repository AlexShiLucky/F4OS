#ifndef BOARD_CONFIG_H_INCLUDED
#define BOARD_CONFIG_H_INCLUDED

#include <stdint.h>

/* Temporary workaround for board configuration until we have device trees */

#define BOARD_CONFIG_VALID_MAGIC 0xdeb1afea

struct lis302dl_accel_config {
    uint32_t valid;
    char *parent_name;
    uint32_t cs_gpio;
    uint8_t cs_active_low;
};

extern struct lis302dl_accel_config lis302dl_accel_config;

#endif
