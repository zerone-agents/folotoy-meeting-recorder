#include "meeting_wifi_sha256.h"
#include "mbedtls/sha256.h"
bool meeting_wifi_sha256(void *ctx,const uint8_t *data,size_t length,uint8_t out[32]){
 (void)ctx;return data&&out&&mbedtls_sha256(data,length,out,0)==0;
}
