#!/usr/bin/env python3
"""Exercise the pinned SRP MPI wrappers using allocation and operation stubs."""
from pathlib import Path
import subprocess,sys,tempfile
source=Path(sys.argv[1]).read_text();start=source.index('int esp_mpi_a_mul_b_mod_c(');functions=source[start:]
prefix=r'''
#include <assert.h>
#include <stdio.h>
typedef struct {int allocated;} mbedtls_mpi;
typedef mbedtls_mpi esp_mpi_t;
typedef mbedtls_mpi esp_mpi_ctx_t;
static int fault,step,live;
static void mbedtls_mpi_init(mbedtls_mpi *p){p->allocated=0;}
static void mbedtls_mpi_free(mbedtls_mpi *p){if(p->allocated){p->allocated=0;live--;}}
static int first(mbedtls_mpi *p){p->allocated=1;live++;return ++step==fault?-1:0;}
static int mbedtls_mpi_mul_mpi(mbedtls_mpi *p,esp_mpi_t *a,esp_mpi_t *b){(void)a;(void)b;return first(p);}
static int mbedtls_mpi_add_mpi(mbedtls_mpi *p,esp_mpi_t *a,esp_mpi_t *b){(void)a;(void)b;return first(p);}
static int mbedtls_mpi_mod_mpi(esp_mpi_t *r,mbedtls_mpi *p,esp_mpi_t *c){(void)r;(void)p;(void)c;return ++step==fault?-1:0;}
'''
suffix=r'''
int main(void){esp_mpi_t a={0};for(int which=0;which<2;which++)for(fault=0;fault<=2;fault++){
 step=live=0;int r=which?esp_mpi_a_add_b_mod_c(&a,&a,&a,&a,&a):esp_mpi_a_mul_b_mod_c(&a,&a,&a,&a,&a);
 assert((fault==0 && r==0)||(fault && r!=0));assert(live==0);
}
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='srp-mpi-cleanup-') as d:
 c=Path(d)/'test.c';out=Path(d)/'test';c.write_text(prefix+functions+suffix)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('SRP MPI cleanup fault paths: PASS (MPI stubbed)')
