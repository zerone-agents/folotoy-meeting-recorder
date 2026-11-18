#include "meeting_upload_session.h"
#include <assert.h>
static int bound=1,cancelled_flag,prepared=1,ran,closed;
static meeting_upload_result run_result=MEETING_UPLOAD_CONFIRMED;
static bool cancelled(void *ctx){(void)ctx;return cancelled_flag;}
void meeting_https_close(meeting_https_config *c){(void)c;closed++;}
bool meeting_https_bind(meeting_https_config *c,meeting_upload_transport *out){if(!c||!bound)return false;*out=(meeting_upload_transport){.ctx=c,.cancelled=cancelled};return true;}
bool meeting_upload_manifest(FILE *f,const meeting_snapshot *s,bool has,int64_t time,uint8_t trust,meeting_manifest *out){(void)f;(void)s;(void)has;(void)time;(void)trust;*out=(meeting_manifest){.count=7};return prepared;}
bool meeting_upload_manifest_verified_cancel(FILE *f,const meeting_snapshot *s,bool ht,int64_t rt,uint8_t trust,meeting_manifest *out,meeting_cancel_fn cancelled,void *ctx){if(cancelled&&cancelled(ctx))return false;return meeting_upload_manifest(f,s,ht,rt,trust,out);}
meeting_upload_result meeting_upload_run_prevalidated(FILE *f,const meeting_snapshot *s,const meeting_manifest *m,const meeting_upload_transport *t){(void)f;(void)s;(void)m;(void)t;ran++;return run_result;}
meeting_upload_result meeting_upload_run_observed(FILE *f,const meeting_snapshot *s,const meeting_manifest *m,const meeting_upload_transport *t,meeting_upload_progress_fn cb,void *ctx){(void)cb;(void)ctx;return meeting_upload_run_prevalidated(f,s,m,t);}
int main(void){FILE *f=tmpfile();assert(f);meeting_snapshot s={0};meeting_https_config c={0};meeting_manifest confirmed={0};assert(meeting_upload_session(f,&s,false,0,0,&c,&confirmed)==MEETING_UPLOAD_CONFIRMED&&ran==1&&closed==1&&confirmed.count==7);run_result=MEETING_UPLOAD_PAUSED;assert(meeting_upload_session(f,&s,false,0,0,&c,NULL)==MEETING_UPLOAD_PAUSED&&ran==2&&closed==2);run_result=MEETING_UPLOAD_CONFIRMED;cancelled_flag=1;assert(meeting_upload_session(f,&s,false,0,0,&c,NULL)==MEETING_UPLOAD_PAUSED&&ran==2);cancelled_flag=0;prepared=0;assert(meeting_upload_session(f,&s,false,0,0,&c,NULL)==MEETING_UPLOAD_FAILED&&ran==2);bound=0;assert(meeting_upload_session(f,&s,false,0,0,&c,NULL)==MEETING_UPLOAD_FAILED);fclose(f);return 0;}
