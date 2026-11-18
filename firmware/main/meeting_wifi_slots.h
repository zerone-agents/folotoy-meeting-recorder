#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Experimental model. Backend must validate checksums/versions, persist each
 * write atomically, and reopen after ambiguous errors; caller holds one lock. */
typedef struct { uint64_t generation; char ssid[33],password[64]; uint8_t request_id[16],digest[32]; } meeting_wifi_slot;
typedef struct { unsigned slot; uint64_t generation; } meeting_wifi_pointer;
typedef struct {
 void *ctx;
 int (*read_slot)(void *,unsigned,meeting_wifi_slot *);
 bool (*write_slot)(void *,unsigned,const meeting_wifi_slot *);
 int (*read_pointer)(void *,meeting_wifi_pointer *);
 bool (*write_pointer)(void *,const meeting_wifi_pointer *);
 bool (*erase_slot)(void *,unsigned);
} meeting_wifi_slots_io;
typedef enum { MEETING_WIFI_SLOTS_OK, MEETING_WIFI_SLOTS_IO_ERROR, MEETING_WIFI_SLOTS_CORRUPT, MEETING_WIFI_SLOTS_INVALID } meeting_wifi_slots_result;
meeting_wifi_slots_result meeting_wifi_slots_load(const meeting_wifi_slots_io *,meeting_wifi_slot *,unsigned *);
/* Only for an externally verified legacy configuration, while normal
 * networking is stopped. The legacy record must remain intact. */
meeting_wifi_slots_result meeting_wifi_slots_initialize(const meeting_wifi_slots_io *,const meeting_wifi_slot *);
meeting_wifi_slots_result meeting_wifi_slots_stage(const meeting_wifi_slots_io *,const meeting_wifi_slot *);
meeting_wifi_slots_result meeting_wifi_slots_commit(const meeting_wifi_slots_io *,uint64_t generation,const uint8_t request_id[16],const uint8_t digest[32]);
bool meeting_wifi_slots_verify_active(const meeting_wifi_slots_io *,uint64_t generation,const uint8_t request_id[16],const uint8_t digest[32]);
bool meeting_wifi_slots_verify_uncommitted(const meeting_wifi_slots_io *,uint64_t generation,const uint8_t request_id[16],const uint8_t digest[32]);
meeting_wifi_slots_result meeting_wifi_slots_cleanup_uncommitted(const meeting_wifi_slots_io *,uint64_t generation,const uint8_t request_id[16],const uint8_t digest[32]);
