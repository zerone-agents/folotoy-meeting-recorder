#!/usr/bin/env python3
"""Exercise extracted IDF salt-generator control flow with deterministic MPI stubs."""
from pathlib import Path
import subprocess, sys, tempfile
source=Path(sys.argv[1]).read_text()
a=source.index('static esp_err_t _esp_srp_gen_salt_verifier(')
b=source.index('\nesp_err_t esp_srp_srv_pubkey(',a)
function=source[a:b]
prefix=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOG_BUFFER_HEX_LEVEL(...) ((void)0)
#define hexdump_mpi(...) ((void)0)
typedef int esp_err_t;
typedef struct {int unused;} esp_mpi_t;
typedef struct {esp_mpi_t *s,*v,*g,*n,*ctx;char *bytes_s;int len_s;} esp_srp_handle_t;
static int output_len=16,random_fails,expected_x_len,short_remaining,calls,alloc_fails,exp_fails;
static esp_mpi_t *esp_mpi_new(void){return calloc(1,sizeof(esp_mpi_t));}
static void esp_mpi_free(esp_mpi_t *p){free(p);}
static int esp_mpi_get_rand(esp_mpi_t *p,int bits,int top,int bottom){(void)p;(void)bits;(void)top;(void)bottom;calls++;return random_fails?-1:0;}
static char *esp_mpi_to_bin(esp_mpi_t *p,int *n){(void)p;if(alloc_fails)return NULL;*n=short_remaining>0?(short_remaining--,15):output_len;return malloc((size_t)*n);}
static esp_mpi_t *calculate_x(char *p,int n,const char *u,int ul,const char *pw,int pl){(void)p;(void)u;(void)ul;(void)pw;(void)pl;expected_x_len=n;return esp_mpi_new();}
static int esp_mpi_a_exp_b_mod_c(esp_mpi_t *v,esp_mpi_t *g,esp_mpi_t *x,esp_mpi_t *n,esp_mpi_t *ctx){(void)v;(void)g;(void)x;(void)n;(void)ctx;return exp_fails?-1:0;}
'''
suffix=r'''
int main(void){
 esp_srp_handle_t h={0}; output_len=15;
#ifdef EXPECT_ORIGINAL
 assert(_esp_srp_gen_salt_verifier(&h,"u",1,"p",1,16)==ESP_OK);assert(h.len_s==16 && output_len==15 && expected_x_len==15);
 free(h.bytes_s);esp_mpi_free(h.s);esp_mpi_free(h.v);
#else
 short_remaining=4;calls=0;assert(_esp_srp_gen_salt_verifier(&h,"u",1,"p",1,16)==ESP_FAIL);assert(!h.bytes_s && !h.s && !h.v && calls==4);
 output_len=16;short_remaining=1;calls=0;expected_x_len=0;assert(_esp_srp_gen_salt_verifier(&h,"u",1,"p",1,16)==ESP_OK && h.len_s==16 && expected_x_len==16 && calls==2);
 free(h.bytes_s);esp_mpi_free(h.s);esp_mpi_free(h.v);memset(&h,0,sizeof h);
 random_fails=1;calls=0;assert(_esp_srp_gen_salt_verifier(&h,"u",1,"p",1,16)==ESP_FAIL && !h.bytes_s && !h.s && calls==1);
 random_fails=0;alloc_fails=1;calls=0;assert(_esp_srp_gen_salt_verifier(&h,"u",1,"p",1,16)==ESP_FAIL && !h.bytes_s && !h.s && calls==1);
 alloc_fails=0;exp_fails=1;calls=0;assert(_esp_srp_gen_salt_verifier(&h,"u",1,"p",1,16)==ESP_FAIL && !h.bytes_s && !h.s && !h.v && calls==1);

#endif
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='srp-short-salt-') as d:
 c=Path(d)/'test.c';out=Path(d)/'test';c.write_text(prefix+function+suffix)
 cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
 if len(sys.argv)>2 and sys.argv[2]=='--original':cmd+=['-DEXPECT_ORIGINAL']
 subprocess.run(cmd+[str(c),'-o',str(out)],check=True);subprocess.run([str(out)],check=True)
print('short salt control-flow test: PASS (MPI and crypto stubbed)')
