#include "meeting_management_ble.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned calls;static uint8_t expected[32];
esp_err_t meeting_pairing_ble_start_management(const uint8_t secret[32],meeting_pairing_handler h,meeting_pairing_reset r,void *ctx) {
    assert(h&&r&&ctx&&memcmp(secret,expected,32)==0);calls++;return ESP_OK;
}
static int read_record(void *ctx,uint8_t out[64]) {memcpy(out,ctx,64);return 0;}
static esp_err_t handler(uint32_t session,const uint8_t *r,size_t n,uint8_t **out,ssize_t *len,void *ctx) {
    (void)session;(void)r;(void)n;(void)out;(void)len;(void)ctx;return ESP_OK;
}
static void reset(void *ctx){(void)ctx;}
int main(void) {
    uint8_t raw[64]; meeting_mgmt_record r={.generation=1,.device_id={1},.secret={2},.state=MEETING_MGMT_ACTIVE};
    memcpy(expected,r.secret,32);assert(meeting_mgmt_encode(&r,raw));meeting_mgmt_io io={.ctx=raw,.read=read_record};
    uint8_t id[16]={1};int ctx=0;
    assert(meeting_management_ble_start(&io,id,handler,reset,&ctx)==ESP_OK&&calls==1);
    id[0]=2;assert(meeting_management_ble_start(&io,id,handler,reset,&ctx)!=ESP_OK&&calls==1);
    id[0]=1;r.state=MEETING_MGMT_REVOKED;memset(r.secret,0,32);assert(meeting_mgmt_encode(&r,raw));
    assert(meeting_management_ble_start(&io,id,handler,reset,&ctx)!=ESP_OK&&calls==1);
    raw[4]^=1;assert(meeting_management_ble_start(&io,id,handler,reset,&ctx)!=ESP_OK&&calls==1);
    puts("management BLE credential gate: PASS");
}
