#include "meeting_config_fragments.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    meeting_config_fragments s={0};const uint8_t *result;size_t length;
    uint8_t bytes[MEETING_CONFIG_MAX_BYTES];assert(meeting_config_fragments_bind(&s,bytes,sizeof bytes));
    assert(!meeting_config_fragments_begin(&s,0,0));
    assert(meeting_config_fragments_begin(&s,0,6));
    assert(meeting_config_fragments_append(&s,0,0,(const uint8_t *)"abc",3));
    assert(!meeting_config_fragments_append(&s,0,0,(const uint8_t *)"abc",3)); /* replay */
    for(size_t i=0;i<s.capacity;i++)assert(s.bytes[i]==0);
    assert(meeting_config_fragments_begin(&s,1,6));
    assert(meeting_config_fragments_append(&s,1,0,(const uint8_t *)"abc",3));
    assert(!meeting_config_fragments_append(&s,2,3,(const uint8_t *)"def",3)); /* new connection */
    assert(meeting_config_fragments_begin(&s,1,6));
    assert(meeting_config_fragments_append(&s,1,0,(const uint8_t *)"abc",3));
    assert(!meeting_config_fragments_seal(&s,1,&result,&length)); /* missing fragment */
    assert(meeting_config_fragments_begin(&s,1,6));
    assert(meeting_config_fragments_append(&s,1,0,(const uint8_t *)"abc",3));
    meeting_config_fragments_reset(&s); /* disconnect, including a reused session handle */
    assert(!meeting_config_fragments_append(&s,1,3,(const uint8_t *)"def",3));
    assert(meeting_config_fragments_begin(&s,1,6));
    assert(meeting_config_fragments_append(&s,1,0,(const uint8_t *)"ab",2));
    assert(meeting_config_fragments_append(&s,1,2,(const uint8_t *)"cdef",4));
    assert(meeting_config_fragments_seal(&s,1,&result,&length));
    assert(length==6&&!memcmp(result,"abcdef",6));
    assert(!meeting_config_fragments_seal(&s,1,&result,&length));
    assert(!s.active&&!s.sealed);
    return 0;
}
