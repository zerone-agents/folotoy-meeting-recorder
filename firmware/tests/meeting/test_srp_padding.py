#!/usr/bin/env python3
"""Exercise the actual IDF SRP padding function with deterministic stubs.

Pass path to esp_srp.c. Cryptography is stubbed; this tests error control flow only.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
start = source.index('static esp_mpi_t *calculate_padded_hash(')
end = source.index('\n/* k =', start)
function = source[start:end]
prefix = r'''
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#define SHA512_HASH_SZ 64
typedef struct { int value; } esp_mpi_t;
typedef struct { int len_n; } esp_srp_handle_t;
typedef struct { int value; } mbedtls_sha512_context;
static int fail_malloc, hash_calls;
static void *test_malloc(size_t n) { return fail_malloc ? NULL : calloc(1,n); }
static void mbedtls_sha512_init(mbedtls_sha512_context *c) {(void)c;}
static void mbedtls_sha512_starts(mbedtls_sha512_context *c,int x) {(void)c;(void)x;}
static void mbedtls_sha512_update(mbedtls_sha512_context *c,const unsigned char *p,size_t n) {(void)c;(void)p;(void)n;hash_calls++;}
static void mbedtls_sha512_finish(mbedtls_sha512_context *c,unsigned char *p) {(void)c;memset(p,0,64);}
static void mbedtls_sha512_free(mbedtls_sha512_context *c) {(void)c;}
static esp_mpi_t result;
static esp_mpi_t *esp_mpi_new_from_bin(char *p,int n) {(void)p;(void)n;return &result;}
#define malloc test_malloc
'''
suffix = r'''
#undef malloc
int main(void) {
 esp_srp_handle_t hd={.len_n=8}; char a[9]={0},b[8]={0};
 fail_malloc=0;hash_calls=0;assert(calculate_padded_hash(&hd,a,8,b,4)==&result);assert(hash_calls>0);
 fail_malloc=1;hash_calls=0;assert(calculate_padded_hash(&hd,a,8,b,4)==NULL);assert(hash_calls==0);
 fail_malloc=0;hash_calls=0;assert(calculate_padded_hash(&hd,a,9,b,4)==NULL);assert(hash_calls==0);
 hash_calls=0;assert(calculate_padded_hash(&hd,a,-1,b,4)==NULL);assert(hash_calls==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='srp-padding-test-') as d:
    c=Path(d)/'test.c'; out=Path(d)/'test';c.write_text(prefix+function+suffix)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('SRP padding control-flow tests passed (stubbed cryptography)')
