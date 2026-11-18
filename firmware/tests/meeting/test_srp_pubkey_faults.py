#!/usr/bin/env python3
"""Fault-inject the pinned SRP public-key generator; cryptography is stubbed."""
from pathlib import Path
import subprocess,sys,tempfile
source=Path(sys.argv[1]).read_text();a=source.index('static esp_err_t __esp_srp_srv_pubkey(');b=source.index('\nstatic esp_err_t _esp_srp_gen_salt_verifier(',a);fn=source[a:b]
prefix=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define hexdump_mpi(...) ((void)0)
typedef int esp_err_t;
typedef struct {int x;} esp_mpi_t;
typedef struct {esp_mpi_t *b,*B,*v,*g,*n,*ctx;char *bytes_B;int len_B;} esp_srp_handle_t;
static int live,op_count,fail_op,fail_rng,fail_serial;
static esp_mpi_t *esp_mpi_new(void){esp_mpi_t *p=calloc(1,sizeof(*p));if(p)live++;return p;}
static void esp_mpi_free(esp_mpi_t *p){if(p){live--;free(p);}}
static esp_mpi_t *calculate_k(esp_srp_handle_t *h){(void)h;return esp_mpi_new();}
static int esp_mpi_get_rand(esp_mpi_t *p,int n,int top,int bottom){(void)p;(void)n;(void)top;(void)bottom;return fail_rng?-1:0;}
static int op(void){return ++op_count==fail_op?-1:0;}
static int esp_mpi_a_mul_b_mod_c(esp_mpi_t *a,esp_mpi_t *b,esp_mpi_t *c,esp_mpi_t *d,esp_mpi_t *e){(void)a;(void)b;(void)c;(void)d;(void)e;return op();}
static int esp_mpi_a_exp_b_mod_c(esp_mpi_t *a,esp_mpi_t *b,esp_mpi_t *c,esp_mpi_t *d,esp_mpi_t *e){(void)a;(void)b;(void)c;(void)d;(void)e;return op();}
static int esp_mpi_a_add_b_mod_c(esp_mpi_t *a,esp_mpi_t *b,esp_mpi_t *c,esp_mpi_t *d,esp_mpi_t *e){(void)a;(void)b;(void)c;(void)d;(void)e;return op();}
static char *esp_mpi_to_bin(esp_mpi_t *p,int *n){(void)p;if(fail_serial)return NULL;*n=1;return malloc(1);}
'''
suffix=r'''
int main(void){
 for(int mode=0;mode<6;mode++){
  esp_srp_handle_t h={0};char *out=NULL;int len=0;live=op_count=fail_rng=fail_serial=fail_op=0;
  if(mode==1)fail_rng=1;else if(mode>=2&&mode<=4)fail_op=mode-1;else if(mode==5)fail_serial=1;
  int rc=__esp_srp_srv_pubkey(&h,&out,&len);
  if(mode==0){assert(rc==ESP_OK&&out&&len==1);free(out);esp_mpi_free(h.b);esp_mpi_free(h.B);}
  else assert(rc!=ESP_OK&&!out&&!h.b&&!h.B);
  assert(live==0);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='srp-public-key-') as d:
 c=Path(d)/'test.c';out=Path(d)/'test';c.write_text(prefix+fn+suffix)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('SRP public key fault paths: PASS (MPI stubbed)')
