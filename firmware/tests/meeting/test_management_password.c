#include "meeting_management_password.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t secret[32]={0};char out[65];memset(out,'x',sizeof(out));
    assert(!meeting_management_password(secret,out));
    for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0);
    secret[0]=0xab;secret[31]=0x1f;
    assert(meeting_management_password(secret,out));
    assert(strlen(out)==64 && !memcmp(out,"ab",2) && !memcmp(out+62,"1f",2));
    assert(!meeting_management_password(NULL,out));
    for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0);
    puts("management secret encoding: PASS");
}
