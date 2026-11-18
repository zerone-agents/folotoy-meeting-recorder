#include "meeting_wifi_slot_apply.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static meeting_wifi_slot stored;static meeting_wifi_pointer pointer;static bool fail_read;
static int read_slot(void *ctx,unsigned i,meeting_wifi_slot *out){(void)ctx;if(fail_read||i!=0)return -1;*out=stored;return 0;}
static int read_ptr(void *ctx,meeting_wifi_pointer *out){(void)ctx;*out=pointer;return 0;}
int main(void){
 meeting_config c={0},before;strcpy(c.ssid,"old");strcpy(c.password,"oldpass");strcpy(c.origin,"https://example.com");strcpy(c.device_token,"do-not-change");c.utc=42;before=c;
 meeting_wifi_slot slot={.generation=2,.ssid="new",.password="new-password"};
 assert(meeting_wifi_slot_apply(&c,&slot));assert(!strcmp(c.ssid,"new")&&!strcmp(c.password,"new-password"));
 strcpy(before.ssid,"new");strcpy(before.password,"new-password");assert(!memcmp(&c,&before,sizeof(c)));
 before=c;memset(slot.ssid,'x',sizeof(slot.ssid));assert(!meeting_wifi_slot_apply(&c,&slot));assert(!memcmp(&c,&before,sizeof(c)));
 stored=(meeting_wifi_slot){.generation=3,.ssid="restored",.password="restored-password"};pointer=(meeting_wifi_pointer){0,3};
 meeting_wifi_slots_io io={.read_slot=read_slot,.read_pointer=read_ptr};
 assert(meeting_wifi_slot_restore(&io,&c));assert(!strcmp(c.ssid,"restored")&&!strcmp(c.origin,"https://example.com"));
 before=c;fail_read=true;assert(!meeting_wifi_slot_restore(&io,&c));assert(!memcmp(&c,&before,sizeof(c)));
 puts("wifi slot preserves device configuration: PASS");
}
