#!/usr/bin/env python3
"""Inject MPI-operation errors into the extracted IDF SRP session function."""
from pathlib import Path
import subprocess
import sys
import tempfile
source = Path(sys.argv[1]).read_text()
start = source.index('esp_err_t esp_srp_get_session_key(')
end = source.index('\nesp_err_t esp_srp_exchange_proofs(', start)
function = source[start:end]
prefix = r'''
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#define SHA512_HASH_SZ 64
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
typedef struct { int x; } esp_mpi_t;
typedef struct { char *bytes_A,*session_key; int len_A; esp_mpi_t *A,*v,*n,*b,*ctx; } esp_srp_handle_t;
static int fault, calls, live, hashes, alloc_fault, alloc_calls;
static void *test_malloc(size_t n) {if(++alloc_calls==alloc_fault)return NULL;return malloc(n);}
#define malloc test_malloc
static esp_mpi_t *esp_mpi_new(void) { esp_mpi_t *p=malloc(sizeof(*p)); if(p)live++;return p; }
static esp_mpi_t *esp_mpi_new_from_bin(char *p,int n) {(void)p;(void)n;return esp_mpi_new();}
static void esp_mpi_free(esp_mpi_t *p) {if(p){live--;free(p);}}
static esp_mpi_t *calculate_u(esp_srp_handle_t *h,char *p,int n) {(void)h;(void)p;(void)n;return esp_mpi_new();}
static void hexdump_mpi(const char *s,esp_mpi_t *p) {(void)s;(void)p;}
static int op(void) {return ++calls==fault ? -1:0;}
static int esp_mpi_a_exp_b_mod_c(esp_mpi_t *a,esp_mpi_t *b,esp_mpi_t *c,esp_mpi_t *d,esp_mpi_t *e) {(void)a;(void)b;(void)c;(void)d;(void)e;return op();}
static int esp_mpi_a_mul_b_mod_c(esp_mpi_t *a,esp_mpi_t *b,esp_mpi_t *c,esp_mpi_t *d,esp_mpi_t *e) {(void)a;(void)b;(void)c;(void)d;(void)e;return op();}
static char *esp_mpi_to_bin(esp_mpi_t *p,int *n) {(void)p;*n=1;return malloc(1);}
static void mbedtls_sha512(unsigned char *p,int n,unsigned char *out,int x) {(void)p;(void)n;(void)x;hashes++;memset(out,0,64);}
'''
suffix = r'''
int main(void) {
 for(fault=0;fault<=3;fault++) {
  esp_srp_handle_t h={0};char input[8]={0};char *key=NULL;uint16_t key_len=0;calls=live=hashes=0;
  alloc_fault=alloc_calls=0;int r=esp_srp_get_session_key(&h,input,sizeof input,&key,&key_len);
  if(fault==0) {assert(r==ESP_OK && key && key_len==64 && hashes==1 && calls==3); free(h.session_key);free(h.bytes_A);esp_mpi_free(h.A);}
  else {assert(r==ESP_FAIL && !key && key_len==0 && hashes==0 && calls==fault);assert(!h.session_key && !h.bytes_A && !h.A);}
  assert(live==0);
 }
 for(alloc_fault=1;alloc_fault<=8;alloc_fault++) {
  esp_srp_handle_t h={0};char input[8]={0};char *key=NULL;uint16_t key_len=0;calls=live=hashes=alloc_calls=0;fault=0;
  int r=esp_srp_get_session_key(&h,input,sizeof input,&key,&key_len);
  assert(r==ESP_FAIL && !key && key_len==0 && hashes==0);
  assert(!h.session_key && !h.bytes_A && !h.A && live==0);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='srp-session-test-') as d:
 c=Path(d)/'test.c'; out=Path(d)/'test';c.write_text(prefix+function+suffix)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('SRP session operation and allocation failure paths passed (stubbed cryptography)')
