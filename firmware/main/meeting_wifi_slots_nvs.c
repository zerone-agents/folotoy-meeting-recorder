#include "meeting_wifi_slots_nvs.h"
#include <string.h>
enum { SLOT_SIZE=161, PTR_SIZE=17 };
static void wipe(void *p,size_t n){volatile uint8_t *q=p;while(n--)*q++=0;}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static void put(uint8_t *p,uint64_t v,unsigned n){while(n--){*p++=(uint8_t)v;v>>=8;}}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static const char *key(unsigned i){return i==0?"slot0":i==1?"slot1":NULL;}
static int read_raw(void *v,const char *k,uint8_t *b,size_t n){if(!v||!k)return -1;
 size_t size=n;esp_err_t e=nvs_get_blob(*(nvs_handle_t*)v,k,b,&size);return e==ESP_ERR_NVS_NOT_FOUND?1:(e==ESP_OK&&size==n?0:-1);}
static bool write_raw(void *v,const char *k,uint8_t *b,size_t n){
 if(!v||!k)return false;
 uint8_t check[SLOT_SIZE];nvs_handle_t h=*(nvs_handle_t*)v;
 bool ok=n<=sizeof(check)&&nvs_set_blob(h,k,b,n)==ESP_OK&&nvs_commit(h)==ESP_OK&&read_raw(v,k,check,n)==0&&!memcmp(b,check,n);
 wipe(check,sizeof(check));return ok;
}
static int read_slot(void *v,unsigned i,meeting_wifi_slot *s){
 if(!s||!key(i))return -1;
 uint8_t b[SLOT_SIZE];int r=read_raw(v,key(i),b,sizeof(b));
 if(r==0){if(memcmp(b,"MWS1",4)||get(b+157,4)!=crc(b,157)||!memchr(b+12,0,33)||!memchr(b+45,0,64))r=-1;
 else {s->generation=get(b+4,8);memcpy(s->ssid,b+12,33);memcpy(s->password,b+45,64);memcpy(s->request_id,b+109,16);memcpy(s->digest,b+125,32);}}
 wipe(b,sizeof(b));return r;
}
static bool write_slot(void *v,unsigned i,const meeting_wifi_slot *s){
 if(!s||!key(i)||!memchr(s->ssid,0,33)||!memchr(s->password,0,64))return false;
 uint8_t b[SLOT_SIZE]={0};memcpy(b,"MWS1",4);put(b+4,s->generation,8);memcpy(b+12,s->ssid,strlen(s->ssid));memcpy(b+45,s->password,strlen(s->password));memcpy(b+109,s->request_id,16);memcpy(b+125,s->digest,32);put(b+157,crc(b,157),4);
 bool ok=write_raw(v,key(i),b,sizeof(b));wipe(b,sizeof(b));return ok;
}
static int read_pointer(void *v,meeting_wifi_pointer *p){
 if(!p)return -1;
 uint8_t b[PTR_SIZE];int r=read_raw(v,"active",b,sizeof(b));
 if(r==0){if(memcmp(b,"MWP1",4)||get(b+13,4)!=crc(b,13)||b[4]>1)r=-1;else {p->slot=b[4];p->generation=get(b+5,8);}}
 wipe(b,sizeof(b));return r;
}
static bool write_pointer(void *v,const meeting_wifi_pointer *p){
 if(!p||p->slot>1||!p->generation)return false;
 uint8_t b[PTR_SIZE]={0};memcpy(b,"MWP1",4);b[4]=(uint8_t)p->slot;put(b+5,p->generation,8);put(b+13,crc(b,13),4);
 bool ok=write_raw(v,"active",b,sizeof(b));wipe(b,sizeof(b));return ok;
}
static bool erase_slot(void *v,unsigned i){
 if(!v||!key(i))return false;
 nvs_handle_t h=*(nvs_handle_t*)v;esp_err_t err=nvs_erase_key(h,key(i));
 if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND)return false;
 if(nvs_commit(h)!=ESP_OK)return false;
 meeting_wifi_slot check={0};int result=read_slot(v,i,&check);wipe(&check,sizeof(check));return result==1;
}
bool meeting_wifi_slots_nvs_bind(nvs_handle_t *h,meeting_wifi_slots_io *out){if(!h||!out)return false;
 *out=(meeting_wifi_slots_io){h,read_slot,write_slot,read_pointer,write_pointer,erase_slot};return true;
 }
