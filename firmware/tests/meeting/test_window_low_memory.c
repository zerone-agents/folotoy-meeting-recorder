#include "meeting_receipt_json.h"
#include "cJSON.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef union { max_align_t alignment; size_t size; } allocation_header;
static size_t live,peak,cap=2048;
static unsigned denied;
static void *limited_malloc(size_t n)
{
 if(n>cap-live){denied++;return NULL;}
 allocation_header *h=malloc(sizeof(*h)+n);if(!h)return NULL;
 h->size=n;live+=n;if(live>peak)peak=live;return h+1;
}
static void limited_free(void *p)
{
 if(p){allocation_header *h=(allocation_header *)p-1;assert(live>=h->size);live-=h->size;free(h);}
}
static char document[4096];
static const char *id="12345678-1234-4123-8123-123456789abc";
static void make_window(void)
{
 size_t used=(size_t)snprintf(document,sizeof(document),"{\"extra\":{\"x\":true},\"chunks\":[");
 for(unsigned i=0;i<16;i++){
  int n=snprintf(document+used,sizeof(document)-used,"%s{\"valid\":true,\"digest\":\"0707070707070707070707070707070707070707070707070707070707070707\",\"size\":1024,\"seq\":%u}",i?",":"",i);
  assert(n>0&&(size_t)n<sizeof(document)-used);used+=(size_t)n;
 }
 int n=snprintf(document+used,sizeof(document)-used,"],\"first\":0,\"id\":\"%s\"}",id);assert(n>0&&(size_t)n<sizeof(document)-used);
}
int main(void)
{
 cJSON_Hooks hooks={.malloc_fn=limited_malloc,.free_fn=limited_free};cJSON_InitHooks(&hooks);
 make_window();meeting_remote_chunk entries[16];
 assert(meeting_chunk_window_json_parse(document,strlen(document),200,id,0,entries));
 assert(!denied&&live==0&&peak<cap);
 for(unsigned i=0;i<16;i++)assert(entries[i].present&&entries[i].valid&&entries[i].size==1024&&entries[i].digest[0]==7);
 char broken[4096];snprintf(broken,sizeof broken,"%s",document);size_t len=strlen(broken);broken[len-1]=',';broken[len]='}';broken[len+1]=0;
 assert(!meeting_chunk_window_json_parse(broken,strlen(broken),200,id,0,entries)&&!entries[0].present);
 const char *duplicate="{\"id\":\"12345678-1234-4123-8123-123456789abc\",\"id\":\"12345678-1234-4123-8123-123456789abc\",\"first\":0,\"chunks\":[]}";
 assert(!meeting_chunk_window_json_parse(duplicate,strlen(duplicate),200,id,0,entries));
 const char *duplicated_seq="{\"id\":\"12345678-1234-4123-8123-123456789abc\",\"first\":0,\"chunks\":[{\"seq\":1,\"seq\":1,\"size\":3,\"digest\":\"0707070707070707070707070707070707070707070707070707070707070707\",\"valid\":true}]}";
 assert(!meeting_chunk_window_json_parse(duplicated_seq,strlen(duplicated_seq),200,id,0,entries));
 const char *bad_windows[]={
  "{\"id\":\"12345678-1234-4123-8123-123456789abc\",\"first\":0,\"chunks\":[{\"seq\":16,\"size\":3,\"digest\":\"0707070707070707070707070707070707070707070707070707070707070707\",\"valid\":true}]}",
  "{\"id\":\"12345678-1234-4123-8123-123456789abc\",\"first\":0,\"chunks\":[{\"seq\":1,\"size\":3,\"digest\":\"0707070707070707070707070707070707070707070707070707070707070707\",\"valid\":1}]}",
  "{\"id\":\"12345678-1234-4123-8123-123456789abc\",\"first\":0,\"chunks\":[],\"extra\":{\"x\":}}",
  "{\"id\":\"12345678-1234-4123-8123-123456789abc\",\"first\":0,\"chunks\":[]}junk",
  "{\"id\":\"12345678-1234-4123-8123-123456789abc\",\"first\":0,\"chunks\":[],\"\\u0069d\":\"12345678-1234-4123-8123-123456789abc\"}"
 };
 for(size_t i=0;i<sizeof(bad_windows)/sizeof(bad_windows[0]);i++){
  memset(entries,0xff,sizeof(entries));
  assert(!meeting_chunk_window_json_parse(bad_windows[i],strlen(bad_windows[i]),200,id,0,entries));
  for(unsigned j=0;j<16;j++)assert(!entries[j].present);
  assert(live==0);
 }
 cap=16;assert(!meeting_chunk_window_json_parse(document,strlen(document),200,id,0,entries)&&!entries[0].present&&live==0&&denied>0);
 cJSON_InitHooks(NULL);printf("bounded window JSON tests passed (peak=%zu bytes)\n",peak);
}
