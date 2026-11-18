#pragma once
#include "meeting_scr1.h"
#include <stdio.h>
typedef bool (*meeting_cancel_fn)(void *ctx);
/* Inspect without writing; caller retains an open handle and must prevent concurrent writers. */
bool meeting_scr1_scan_file(FILE *file, meeting_scr1_scan_result *out);
bool meeting_scr1_scan_file_cancel(FILE *file, meeting_scr1_scan_result *out, meeting_cancel_fn cancelled, void *ctx);
/* Observer receives each physical source byte once in order. Its output is
 * tentative until the scan succeeds; a failed scan must discard that output. */
typedef bool (*meeting_scr1_observe_fn)(void *ctx,const uint8_t *data,size_t length);
bool meeting_scr1_scan_file_observe(FILE *file, meeting_scr1_scan_result *out,
                                   meeting_cancel_fn cancelled,void *cancel_ctx,
                                   meeting_scr1_observe_fn observe,void *observe_ctx);
/* Read projected bytes at absolute offset; short reads and bounds violations fail. */
bool meeting_scr1_read_projected(FILE *file, const meeting_scr1_scan_result *scan,
                                 uint64_t offset, uint8_t *out, size_t length);
