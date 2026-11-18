#define _POSIX_C_SOURCE 200809L
#include "meeting_scr1_file.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
typedef struct {unsigned char *data;size_t used,capacity;} observer;
static bool observe(void *ctx,const uint8_t *data,size_t length){observer *o=ctx;if(length>o->capacity-o->used)return false;memcpy(o->data+o->used,data,length);o->used+=length;return true;}
int main(void){
 unsigned char original[105]={0},copy[105],projected[104];memcpy(original,"SCR1",4);original[4]=1;memset(original+20,255,4);memset(original+24,0xa5,48);original[96]=3;original[97]=0xf8;original[98]=0xff;original[99]=0xfe;original[100]=3;original[101]=0xf8;original[102]=0xff;original[103]=0xfe;original[104]=3;
 FILE *f=tmpfile();assert(f);assert(fwrite(original,1,sizeof original,f)==sizeof original);assert(fflush(f)==0);meeting_scr1_scan_result scan;assert(meeting_scr1_scan_file(f,&scan));assert(scan.frames==2&&scan.valid_end==104&&scan.incomplete_tail);
 observer all={.data=copy,.capacity=sizeof copy};assert(meeting_scr1_scan_file_observe(f,&scan,NULL,NULL,observe,&all));assert(all.used==sizeof original&&!memcmp(copy,original,sizeof original));
 for(size_t i=0;i<sizeof projected;i+=7){size_t n=sizeof projected-i;if(n>7)n=7;assert(meeting_scr1_read_projected(f,&scan,i,projected+i,n));}
 assert(projected[20]==2&&projected[21]==0);for(size_t i=24;i<72;i++)assert(projected[i]==0);assert(!meeting_scr1_read_projected(f,&scan,104,projected,1));
 meeting_scr1_scan_result oversized={.valid_end=UINT64_MAX,.frames=2};
 assert(!meeting_scr1_read_projected(f,&oversized,0,projected,(size_t)INT_MAX+1u));
 assert(fseek(f,0,SEEK_SET)==0);assert(fread(copy,1,sizeof copy,f)==sizeof copy);assert(memcmp(copy,original,sizeof copy)==0);assert(fclose(f)==0);return 0;
}
