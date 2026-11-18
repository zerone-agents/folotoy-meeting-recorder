#include "meeting_scr1_file.h"
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

static int read_at(void *ctx, uint64_t offset, uint8_t *out, size_t length)
{
    FILE *file = ctx;
    if (offset > LONG_MAX || length > INT_MAX || fseek(file, (long)offset, SEEK_SET) != 0) return -1;
    size_t got = fread(out, 1, length, file);
    if (got != length) return -1;
    return (int)got;
}

/* SCR1 scanning visits each frame header and packet in order. Buffer reads so
 * validation does not issue two filesystem seeks per frame on long recordings. */
typedef struct {
    FILE *file;
    meeting_cancel_fn cancelled;
    void *ctx;
    uint64_t file_size, start;
    uint64_t observed;
    size_t length;
    meeting_scr1_observe_fn observe;
    void *observe_ctx;
    uint8_t buffer[1024];
} scan_context;
static int read_at_cancel(void *ctx,uint64_t offset,uint8_t *out,size_t length)
{
    scan_context *s=ctx;
    if(s->cancelled && s->cancelled(s->ctx))return -1;
    if(offset>s->file_size || length>s->file_size-offset || length>sizeof(s->buffer))return -1;
    if(offset<s->start || offset-s->start>s->length || length>s->length-(size_t)(offset-s->start)){
        if(offset>LONG_MAX || fseek(s->file,(long)offset,SEEK_SET)!=0)return -1;
        size_t wanted=s->file_size-offset>sizeof(s->buffer)?sizeof(s->buffer):(size_t)(s->file_size-offset);
        size_t got=fread(s->buffer,1,wanted,s->file);
        if(got!=wanted)return -1;
        if(s->observe){
            if(offset>s->observed)return -1;
            if(offset+got>s->observed){
                size_t skip=(size_t)(s->observed-offset);
                if(!s->observe(s->observe_ctx,s->buffer+skip,got-skip))return -1;
                s->observed=offset+got;
            }
        }
        s->start=offset;s->length=got;
    }
    memcpy(out,s->buffer+(size_t)(offset-s->start),length);
    return (int)length;
}

bool meeting_scr1_scan_file(FILE *file, meeting_scr1_scan_result *out)
{
    return meeting_scr1_scan_file_cancel(file,out,NULL,NULL);
}

bool meeting_scr1_scan_file_cancel(FILE *file, meeting_scr1_scan_result *out, meeting_cancel_fn cancelled, void *ctx)
{
    return meeting_scr1_scan_file_observe(file,out,cancelled,ctx,NULL,NULL);
}

bool meeting_scr1_scan_file_observe(FILE *file, meeting_scr1_scan_result *out,
                                    meeting_cancel_fn cancelled,void *ctx,
                                    meeting_scr1_observe_fn observe,void *observe_ctx)
{
    struct stat st;
    if (!file || !out || fstat(fileno(file), &st) != 0 || st.st_size < 0) return false;
    scan_context scan={.file=file,.cancelled=cancelled,.ctx=ctx,.file_size=(uint64_t)st.st_size,
                       .observe=observe,.observe_ctx=observe_ctx};
    meeting_scr1_scan_result result;
    if(!meeting_scr1_scan(read_at_cancel, &scan, (uint64_t)st.st_size, &result) ||
       (cancelled && cancelled(ctx)) || (observe && scan.observed!=(uint64_t)st.st_size))return false;
    *out=result;return true;
}

bool meeting_scr1_read_projected(FILE *file, const meeting_scr1_scan_result *scan,
                                 uint64_t offset, uint8_t *out, size_t length)
{
    if (!file || !scan || (!out && length) || length > INT_MAX || offset > scan->valid_end ||
        length > scan->valid_end - offset) return false;
    if (length && read_at(file, offset, out, length) != (int)length) return false;
    return meeting_scr1_project(offset, out, length, scan->valid_end, scan->frames);
}
