#pragma once
#include "meeting_config.h"
/* Input must already be parsed and fingerprint-verified. Selects the
 * equivalent X2 root for X1 configurations; all other roots are unchanged. */
const char *meeting_ca_compat_preferred(const meeting_config *config);
