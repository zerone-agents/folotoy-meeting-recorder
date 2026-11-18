#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "meeting_wifi_slots.h"
typedef enum {MEETING_WIFI_COMMAND_SWITCH=1,MEETING_WIFI_COMMAND_QUERY=2,MEETING_WIFI_COMMAND_ACK=3} meeting_wifi_command_kind;
typedef struct {meeting_wifi_command_kind kind;uint8_t operation_id[16];char ssid[33],password[64];} meeting_wifi_command;
/* Input must be a complete authenticated and decrypted session message.
 * Output contains credentials; caller must erase it after use. */
bool meeting_wifi_command_parse(const uint8_t *input,size_t size,meeting_wifi_command *out);
void meeting_wifi_command_clear(meeting_wifi_command *command);
/* Hash canonical domain || operation ID || length-prefixed SSID/password.
 * The production callback must be SHA-256; output is zero on failure. */
bool meeting_wifi_command_digest(const meeting_wifi_command *command,
    bool (*sha256)(void *,const uint8_t *,size_t,uint8_t[32]),void *ctx,uint8_t out[32]);
bool meeting_wifi_command_to_slot(const meeting_wifi_command *command,uint64_t generation,
    bool (*sha256)(void *,const uint8_t *,size_t,uint8_t[32]),void *ctx,meeting_wifi_slot *out);
