#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool meeting_wifi_sha256(void *ctx,const uint8_t *data,size_t length,uint8_t out[32]);
