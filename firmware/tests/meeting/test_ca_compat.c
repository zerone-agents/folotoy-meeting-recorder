#include "meeting_ca_compat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const uint8_t x1[32]={
        0x96,0xbc,0xec,0x06,0x26,0x49,0x76,0xf3,0x74,0x60,0x77,0x9a,0xcf,0x28,0xc5,0xa7,
        0xcf,0xe8,0xa3,0xc0,0xaa,0xe1,0x1a,0x8f,0xfc,0xee,0x05,0xc0,0xbd,0xdf,0x08,0xc6};
    meeting_config c={0};
    strcpy(c.ca_pem,"custom root");
    assert(meeting_ca_compat_preferred(&c)==c.ca_pem&&!strcmp(c.ca_pem,"custom root"));
    memcpy(c.ca_sha256,x1,sizeof(x1));
    strcpy(c.ca_pem,"original x1 root");
    const char *preferred=meeting_ca_compat_preferred(&c);
    assert(preferred&&preferred!=c.ca_pem&&!strncmp(preferred,"-----BEGIN CERTIFICATE-----",27));
    assert(!strcmp(c.ca_pem,"original x1 root"));
    memset(c.ca_pem,'b',sizeof(c.ca_pem));
    assert(!meeting_ca_compat_preferred(&c));
    assert(!meeting_ca_compat_preferred(NULL));
    puts("CA compatibility bundle: PASS");
}
