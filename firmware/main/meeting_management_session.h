#pragma once
#include "meeting_management_credential.h"
#include "meeting_wifi_operation.h"
#include "esp_err.h"
typedef struct meeting_management_session meeting_management_session;
typedef enum {MEETING_SESSION_IDLE,MEETING_SESSION_QUERY,MEETING_SESSION_PREPARED,MEETING_SESSION_ERROR} meeting_session_result;
/* All APIs are called by one coordinator task after STA has stopped. */
meeting_management_session *meeting_management_session_create(meeting_wifi_switch *,const meeting_wifi_slots_io *,meeting_wifi_operation *);
esp_err_t meeting_management_session_start(meeting_management_session *,const meeting_mgmt_io *,const uint8_t id[16]);
meeting_session_result meeting_management_session_poll(meeting_management_session *);
bool meeting_management_session_acknowledged(meeting_management_session *);
/* On failure retain the object and retry stop; callbacks may still own it. */
esp_err_t meeting_management_session_stop(meeting_management_session *);
bool meeting_management_session_destroy(meeting_management_session *);
