#include "meeting_ca_compat.h"
#include <string.h>

/* Official ISRG Root X2 self-signed certificate. It is cross-signed by
 * ISRG Root X1 and therefore represents an already trusted subordinate key. */
static const char root_x2[] = "-----BEGIN CERTIFICATE-----\nMIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\nCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\nR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\nMDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\nZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\nEAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\nItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\nAQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\nzj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\ntL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n/q4AaOeMSQ+2b1tbFfLn\n-----END CERTIFICATE-----\n";
static const uint8_t root_x1_sha256[32]={
 0x96,0xbc,0xec,0x06,0x26,0x49,0x76,0xf3,0x74,0x60,0x77,0x9a,0xcf,0x28,0xc5,0xa7,
 0xcf,0xe8,0xa3,0xc0,0xaa,0xe1,0x1a,0x8f,0xfc,0xee,0x05,0xc0,0xbd,0xdf,0x08,0xc6};
const char *meeting_ca_compat_preferred(const meeting_config *config)
{
 if(!config||!config->ca_pem[0]||!memchr(config->ca_pem,0,sizeof(config->ca_pem)))return NULL;
 return memcmp(config->ca_sha256,root_x1_sha256,sizeof(root_x1_sha256))?config->ca_pem:root_x2;
}
