#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Caller wipes output after use. Input must originate from a provisioned,
 * device-specific high-entropy credential, not from a user-chosen password. */
bool meeting_management_password(const uint8_t secret[32],char out[65]);
