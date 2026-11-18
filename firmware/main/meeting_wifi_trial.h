#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {MEETING_TRIAL_IDLE,MEETING_TRIAL_RUNNING,MEETING_TRIAL_CONNECTED,
              MEETING_TRIAL_INTERRUPTED,MEETING_TRIAL_TIMEOUT,MEETING_TRIAL_STOP_FAILED,
              MEETING_TRIAL_START_FAILED} meeting_trial_result;
typedef struct {void *ctx;bool (*start)(void *,const char *,const char *);bool (*stop)(void *);bool (*has_ip)(void *);} meeting_wifi_trial_io;
typedef struct {meeting_wifi_trial_io io;meeting_trial_result state;uint64_t since,timeout;} meeting_wifi_trial;
/* Caller must stop old STA first and ensure has_ip represents the new
 * connection. Calls are serialized; no credentials are stored here. */
meeting_trial_result meeting_wifi_trial_start(meeting_wifi_trial *,const meeting_wifi_trial_io *,const char *,const char *,uint64_t now,uint64_t timeout);
meeting_trial_result meeting_wifi_trial_poll(meeting_wifi_trial *,uint64_t now,bool record_requested);
