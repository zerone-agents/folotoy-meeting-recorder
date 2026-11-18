#!/usr/bin/env python3
"""Exercise the pinned Security 2 RNG helper with failing and successful stubs."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
start = source.index('static bool sec2_random_session_id(')
end = source.index('static esp_err_t handle_session_command1(', start)
function = source[start:end]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
typedef struct { void *p; } mbedtls_entropy_context;
typedef struct { void *p; } mbedtls_ctr_drbg_context;
static int fail_seed,fail_random,entropy_frees,drbg_frees,random_calls,live;
#define ESP_LOGE(tag,...) ((void)0)
static void mbedtls_entropy_init(mbedtls_entropy_context *c){c->p=NULL;}
static void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *c){c->p=NULL;}
static int mbedtls_entropy_func(void *c,unsigned char *b,size_t n){(void)c;(void)b;(void)n;return 0;}
static int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *d,int (*f)(void *,unsigned char *,size_t),void *arg,const unsigned char *x,size_t n){
 (void)f;(void)x;(void)n;mbedtls_entropy_context *e=arg;
 e->p=malloc(216);d->p=malloc(32);assert(e->p&&d->p);live+=2;return fail_seed?-1:0;
}
static int mbedtls_ctr_drbg_random(mbedtls_ctr_drbg_context *d,unsigned char *out,size_t n){
 (void)d;(void)out;(void)n;random_calls++;return fail_random?-1:0;
}
static void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *d){drbg_frees++;if(d->p){free(d->p);d->p=NULL;live--;}}
static void mbedtls_entropy_free(mbedtls_entropy_context *e){entropy_frees++;if(e->p){free(e->p);e->p=NULL;live--;}}
'''
suffix = r'''
int main(void){unsigned char out[8];
 for(int mode=0;mode<3;mode++){
  fail_seed=mode==1;fail_random=mode==2;entropy_frees=drbg_frees=random_calls=live=0;
  bool ok=sec2_random_session_id(out,sizeof out);
  assert(ok==(mode==0));assert(entropy_frees==1&&drbg_frees==1&&live==0);
  assert(random_calls==(mode==1?0:1));
 }
}
'''
with tempfile.TemporaryDirectory(prefix='security2-rng-test-') as directory:
    c = Path(directory) / 'test.c'
    out = Path(directory) / 'test'
    c.write_text(prefix + function + suffix)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('Security 2 RNG cleanup: PASS (stubbed entropy and RNG)')
