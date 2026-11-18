#define _POSIX_C_SOURCE 200809L
#include "meeting_scr1_file.h"
#include <assert.h>
#include <string.h>
static bool cancel_after(void *ctx){unsigned *remaining=ctx;return --*remaining==0;}
typedef struct {uint8_t *data;size_t used,capacity;} observer;
static bool observe(void *ctx,const uint8_t *data,size_t length){observer *o=ctx;if(length>o->capacity-o->used)return false;memcpy(o->data+o->used,data,length);o->used+=length;return true;}
int main(void){
 enum {FRAMES=50, SIZE=96+FRAMES*161};uint8_t source[SIZE]={0},projected[SIZE],actual[SIZE],block[4096];
 memcpy(source,"SCR1",4);source[4]=1;memset(source+20,0xff,4);memset(source+24,0xa5,48);
 for(unsigned i=0;i<FRAMES;i++){size_t p=96+i*161;source[p]=160;source[p+1]=0xf8;memset(source+p+2,(int)i,159);}
 FILE *f=tmpfile();assert(f&&fwrite(source,1,sizeof(source),f)==sizeof(source)&&fflush(f)==0);
 meeting_scr1_scan_result scan;assert(meeting_scr1_scan_file(f,&scan)&&scan.frames==FRAMES&&scan.valid_end==SIZE);
 observer all={.data=actual,.capacity=SIZE};
 assert(meeting_scr1_scan_file_observe(f,&scan,NULL,NULL,observe,&all));
 assert(all.used==SIZE&&!memcmp(actual,source,SIZE));
 /* Cancellation remains observable on cache hits and after cache refills;
  * neither interrupted scan may publish a partial result. */
 for(unsigned n=2;n<=30;n+=28){
  unsigned remaining=n;meeting_scr1_scan_result partial={.valid_end=42,.frames=7,.incomplete_tail=true};
  assert(!meeting_scr1_scan_file_cancel(f,&partial,cancel_after,&remaining));
  assert(partial.valid_end==42&&partial.frames==7&&partial.incomplete_tail);
 }
 memcpy(projected,source,SIZE);projected[20]=FRAMES;projected[21]=projected[22]=projected[23]=0;memset(projected+24,0,48);
 /* Read in reverse order and retry each block, as after a resumed upload. */
 for(int i=(SIZE-1)/4096;i>=0;i--){size_t offset=(size_t)i*4096,n=SIZE-offset>4096?4096:SIZE-offset;
  assert(meeting_scr1_read_projected(f,&scan,offset,block,n)&&!memcmp(block,projected+offset,n));
  assert(meeting_scr1_read_projected(f,&scan,offset,actual+offset,n)&&!memcmp(block,actual+offset,n));
 }
 assert(!memcmp(actual,projected,SIZE));assert(fseek(f,0,SEEK_SET)==0&&fread(actual,1,SIZE,f)==SIZE&&!memcmp(actual,source,SIZE));
 /* A malformed packet after several buffer refills must still be rejected. */
 assert(fseek(f,96+20*161+1,SEEK_SET)==0&&fputc(0xfc,f)!=EOF&&fflush(f)==0);
 assert(!meeting_scr1_scan_file(f,&scan));
 return fclose(f)!=0;
}
