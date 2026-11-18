#include "meeting_config_protocol.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    meeting_config_fragments state={0};
    uint8_t bytes[MEETING_CONFIG_MAX_BYTES];assert(meeting_config_fragments_bind(&state,bytes,sizeof bytes));
    const uint8_t start[]={1,4,0},data1[]={2,0,0,'a','b'},data2[]={2,2,0,'c','d'},end[]={3};
    assert(meeting_config_protocol_request(&state,11,start,sizeof start));
    assert(meeting_config_protocol_request(&state,11,data1,sizeof data1));
    assert(!meeting_config_protocol_request(&state,11,data1,sizeof data1)); /* replay wipes partial data */
    assert(!state.active && state.bytes[0]==0);
    assert(meeting_config_protocol_request(&state,11,start,sizeof start));
    assert(meeting_config_protocol_request(&state,11,data1,sizeof data1));
    assert(meeting_config_protocol_request(&state,11,data2,sizeof data2));
    assert(meeting_config_protocol_request(&state,11,end,sizeof end));
    assert(state.sealed&&state.received==4&&!memcmp(state.bytes,"abcd",4));
    assert(!meeting_config_protocol_request(&state,11,end,sizeof end));assert(!state.sealed);
    const uint8_t malformed[]={1,4};assert(!meeting_config_protocol_request(&state,11,malformed,sizeof malformed));
    return 0;
}
