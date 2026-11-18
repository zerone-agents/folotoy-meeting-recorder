#include "meeting_scr1.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
int main(void) {
    uint8_t input[110], result[110], one[110];
    for (size_t i=0; i<sizeof(input); ++i) input[i]=(uint8_t)(i+1);
    memcpy(result,input,sizeof(result)); memcpy(one,input,sizeof(one));
    assert(meeting_scr1_project(0,one,sizeof(one),sizeof(one),0x1234));
    const size_t offsets[]={0, 19, 21, 23, 25, 71, 73, 110};
    for(size_t i=0;i<7;i++) assert(meeting_scr1_project(offsets[i],result+offsets[i],offsets[i+1]-offsets[i],sizeof(result),0x1234));
    assert(memcmp(one,result,sizeof(one))==0);
    for(size_t i=0;i<sizeof(result);i++) {
        uint8_t expected = i==20 ? 0x34 : i==21 ? 0x12 : i==22 || i==23 || (i>=24 && i<72) ? 0 : input[i];
        assert(result[i]==expected);
    }
    /* Every valid subrange must match the byte-by-byte reference, including
     * ranges crossing either edge of both rewritten header fields. */
    for(size_t offset=0;offset<=sizeof(input);offset++){
        for(size_t length=0;length<=sizeof(input)-offset;length++){
            memcpy(result,input,sizeof(input));
            assert(meeting_scr1_project(offset,result+offset,length,sizeof(input),0x1234));
            for(size_t i=0;i<sizeof(input);i++){
                uint8_t expected=input[i];
                if(i>=offset&&i<offset+length){
                    if(i>=20&&i<24)expected=(uint8_t)(0x1234u>>((i-20)*8));
                    else if(i>=24&&i<72)expected=0;
                }
                assert(result[i]==expected);
            }
        }
    }
    assert(!meeting_scr1_project(UINT64_MAX,result,1,110,1));
    assert(!meeting_scr1_project(109,result,2,110,1));
    assert(!meeting_scr1_project(0,NULL,5,110,1));
    assert(!meeting_scr1_project(0,result,5,110,0));
    assert(!meeting_scr1_project(0,result,5,110,90001));
    return 0;
}
