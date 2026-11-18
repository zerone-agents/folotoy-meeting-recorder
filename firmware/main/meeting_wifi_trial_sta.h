#pragma once
#include "meeting_wifi_trial.h"
typedef struct {bool owned;} meeting_wifi_trial_sta;
/* Caller serializes all STA users and first waits for the upload supervisor
 * to relinquish STA; do not bind to a live supervisor without that barrier. */
bool meeting_wifi_trial_sta_bind(meeting_wifi_trial_sta *owner,meeting_wifi_trial_io *out);
