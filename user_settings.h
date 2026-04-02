/* user_settings.h - WolfSSL configuration for NOSgem / DOS + OpenWatcom */

#ifndef WOLFSSL_USER_SETTINGS_H
#define WOLFSSL_USER_SETTINGS_H

/* --- Compiler / Platform --- */
#define WOLFSSL_WATCOM
#define WC_16BIT_CPU            /* word32 = unsigned long (32-bit even in 16-bit mode) */
#define USE_INTEGER_HEAP_MATH   /* SP math does not support 16-bit; use portable heap math */
#define SINGLE_THREADED

/* --- Integer math digit size ---
 * MP_16BIT: mp_digit=unsigned int (16-bit), mp_word=unsigned long (32-bit).
 * DIGIT_BIT must be a numeric constant -- sizeof() in #if is not valid C
 * preprocessing and OpenWatcom rejects it strictly.
 * 15 = CHAR_BIT * sizeof(unsigned int) - 1 = 8*2 - 1, leaving 1 overflow bit.
 */
#define MP_16BIT
#define DIGIT_BIT 15
#define WOLFSSL_SMALL_STACK
#define WOLFSSL_USER_IO
#define NO_FILESYSTEM
#define NO_DEV_RANDOM
#define CUSTOM_RAND_GENERATE    nos_rand_byte
unsigned char nos_rand_byte(void);

/* --- TLS version: 1.2 only --- */
#define WOLFSSL_TLS13           /* keep 1.3 off for now, enable later if needed */
#undef  WOLFSSL_TLS13
#define NO_OLD_TLS              /* disables TLS 1.0 and 1.1 */

/* --- Cipher suite trimming --- */
/* Keep: AES-GCM, ECC (for ECDHE key exchange), SHA-256 */
#define NO_DH       /* use ECDHE only; saves dh.c from needing to be compiled */
#define NO_DSA
#define NO_RC4
#define NO_HC128
#define NO_RABBIT
#define NO_DES3
#define NO_PSK
#define NO_MD4
#define NO_MD5          /* not needed for TLS 1.2 with SHA-256 */
#define NO_SHA          /* SHA-1 not needed */
/* NO_RSA removed: geminiprotocol.net uses RSA cert; needed for broad compatibility */

/* AES-GCM is the primary cipher */
#define HAVE_AESGCM
#define HAVE_AES_DECRYPT

/* ECC for key exchange (ECDHE) and authentication */
#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define TFM_TIMING_RESISTANT

/* SHA-256 for handshake and TOFU fingerprints */
#define NO_SHA384       /* save space */
#define NO_SHA512       /* save space */

/* --- Memory --- */
#define WOLFSSL_SMALL_STACK_STATIC
/* ALT_ECC_SIZE removed: incompatible with USE_INTEGER_HEAP_MATH */
/* TFM_ECC256 removed: TFM is a fast-math define, conflicts with heap math */
/* WOLFSSL_SMALL_STACK_CACHE removed: incompatible with USE_INTEGER_HEAP_MATH
   (DECL_MP_INT_SIZE_DYN declares arrays; cache tries to assign to them) */

/* --- TLS extensions --- */
#define HAVE_TLS_EXTENSIONS     /* required for SNI and other handshake extensions */
#define HAVE_SNI                /* Server Name Indication -- needed for virtual hosting */
#define HAVE_SUPPORTED_CURVES   /* Supported Groups extension -- required for ECDHE negotiation */
#define HAVE_EXTENDED_MASTER    /* Extended Master Secret -- required by many modern servers */
#define WOLFSSL_ALERT_WATCH     /* record last sent/received alert for diagnostics */

/* --- Reduce code size --- */
#define NO_WOLFSSL_SERVER   /* client-only build */
#define NO_SESSION_CACHE
#define WOLFSSL_NO_SOCK
#define WOLFSSL_BASE16      /* needed for fingerprint display */

/* --- DOS-specific --- */
#define WOLFSSL_HAVE_MIN
#define WOLFSSL_HAVE_MAX
#define NO_WRITEV
#define NO_MAIN_DRIVER
#define BENCH_EMBEDDED

#endif /* WOLFSSL_USER_SETTINGS_H */
