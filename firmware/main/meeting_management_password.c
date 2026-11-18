#include "meeting_management_password.h"
#include <string.h>
bool meeting_management_password(const uint8_t secret[32],char out[65]) {
    static const char hex[]="0123456789abcdef";
    if(!out)return false;
    memset(out,0,65);
    if(!secret)return false;
    unsigned any=0;for(unsigned i=0;i<32;i++)any|=secret[i];
    if(!any)return false;
    for(unsigned i=0;i<32;i++){out[2*i]=hex[secret[i]>>4];out[2*i+1]=hex[secret[i]&15];}
    return true;
}
