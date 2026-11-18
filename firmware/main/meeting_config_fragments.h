#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The BLE transport must reset on disconnect before another session is
 * accepted, including when a connection handle is reused. Each message is
 * already authenticated and replay-protected by the transport. */
#define MEETING_CONFIG_MAX_BYTES 6144u
typedef struct {
    uint8_t *bytes;
    size_t capacity;
    uint32_t session;
    uint16_t expected,received;
    bool active,sealed;
} meeting_config_fragments;
bool meeting_config_fragments_bind(meeting_config_fragments *state,uint8_t *bytes,size_t capacity);
void meeting_config_fragments_reset(meeting_config_fragments *state);
bool meeting_config_fragments_begin(meeting_config_fragments *state,uint32_t session,uint16_t length);
bool meeting_config_fragments_append(meeting_config_fragments *state,uint32_t session,uint16_t offset,
                                     const uint8_t *bytes,size_t length);
/* The returned pointer is owned by state and must be copied before reset.
 * Sealing alone does not authorize parsing, storing, or applying a config. */
bool meeting_config_fragments_seal(meeting_config_fragments *state,uint32_t session,
                                   const uint8_t **bytes,size_t *length);
