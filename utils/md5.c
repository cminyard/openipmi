/* md5.c - MD5 Message-Digest Algorithm
 * Copyright (C) 1995,1996,1998,1999,2001,2002 Free Software Foundation, Inc.
 *
 * Modifications for IPMI are Copyright(C) 2002,2003 MontaVista Software.
 * Corey Minyard <cminyard@mvista.com>
 *
 * This file is part of Libgcrypt.
 *
 * Libgcrypt is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * Libgcrypt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * According to the definition of MD5 in RFC 1321 from April 1992.
 * NOTE: This is *not* the same file as the one from glibc.
 * Written by Ulrich Drepper <drepper@gnu.ai.mit.edu>, 1995. 
 * heavily modified for GnuPG by Werner Koch <wk@gnupg.org> 
 */

/* Test values:
 * ""                  D4 1D 8C D9 8F 00 B2 04  E9 80 09 98 EC F8 42 7E
 * "a"                 0C C1 75 B9 C0 F1 B6 A8  31 C3 99 E2 69 77 26 61
 * "abc                90 01 50 98 3C D2 4F B0  D6 96 3F 7D 28 E1 7F 72
 * "message digest"    F9 6B 69 7D 7C B7 93 8D  52 5A 2F 31 AA F1 61 D0
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <OpenIPMI/internal/md5.h>

typedef uint32_t u32;
typedef uint8_t  byte;
#define rol(x,n) ( ((x) << (n)) | ((x) >> (32-(n))) )

typedef struct {
    u32 A,B,C,D;	  /* chaining variables */
    u32  nblocks;
    byte buf[64];
    int  count;
} MD5_CONTEXT;


static void
md5_init( MD5_CONTEXT *ctx )
{
    ctx->A = 0x67452301;
    ctx->B = 0xefcdab89;
    ctx->C = 0x98badcfe;
    ctx->D = 0x10325476;

    ctx->nblocks = 0;
    ctx->count = 0;
}

static void
burn_stack (int bytes)
{
    char buf[128];
    
    memset (buf, 0, sizeof buf);
    bytes -= sizeof buf;
    if (bytes > 0)
        burn_stack (bytes);
}



/* These are the four functions used in the four steps of the MD5 algorithm
   and defined in the RFC 1321.  The first function is a little bit optimized
   (as found in Colin Plumbs public domain implementation).  */
/* #define FF(b, c, d) ((b & c) | (~b & d)) */
#define FF(b, c, d) (d ^ (b & (c ^ d)))
#define FG(b, c, d) FF (d, b, c)
#define FH(b, c, d) (b ^ c ^ d)
#define FI(b, c, d) (c ^ (b | ~d))


/****************
 * transform n*64 bytes
 */
static void
/*transform( MD5_CONTEXT *ctx, const void *buffer, size_t len )*/
transform( MD5_CONTEXT *ctx, byte *data )
{
    u32 correct_words[16];
    register u32 A = ctx->A;
    register u32 B = ctx->B;
    register u32 C = ctx->C;
    register u32 D = ctx->D;
    u32 *cwp = correct_words;
    int i;
    byte *p1;

    for(i=0, p1=data; i < 16; i++, p1 += 4)
	correct_words[i] = p1[0] | (p1[1] << 8) | (p1[2] << 16) | (p1[3] << 24);

#define OP(a, b, c, d, s, T)					    \
  do								    \
    {								    \
      a += FF (b, c, d) + (*cwp++) + T; 	    \
      a = rol(a, s);						    \
      a += b;							    \
    }								    \
  while (0)

    /* Before we start, one word about the strange constants.
       They are defined in RFC 1321 as

       T[i] = (int) (4294967296.0 * fabs (sin (i))), i=1..64
     */

    /* Round 1.  */
    OP (A, B, C, D,  7, 0xd76aa478);
    OP (D, A, B, C, 12, 0xe8c7b756);
    OP (C, D, A, B, 17, 0x242070db);
    OP (B, C, D, A, 22, 0xc1bdceee);
    OP (A, B, C, D,  7, 0xf57c0faf);
    OP (D, A, B, C, 12, 0x4787c62a);
    OP (C, D, A, B, 17, 0xa8304613);
    OP (B, C, D, A, 22, 0xfd469501);
    OP (A, B, C, D,  7, 0x698098d8);
    OP (D, A, B, C, 12, 0x8b44f7af);
    OP (C, D, A, B, 17, 0xffff5bb1);
    OP (B, C, D, A, 22, 0x895cd7be);
    OP (A, B, C, D,  7, 0x6b901122);
    OP (D, A, B, C, 12, 0xfd987193);
    OP (C, D, A, B, 17, 0xa679438e);
    OP (B, C, D, A, 22, 0x49b40821);

#undef OP
#define OP(f, a, b, c, d, k, s, T)  \
    do								      \
      { 							      \
	a += f (b, c, d) + correct_words[k] + T;		      \
	a = rol(a, s);						      \
	a += b; 						      \
      } 							      \
    while (0)

    /* Round 2.  */
    OP (FG, A, B, C, D,  1,  5, 0xf61e2562);
    OP (FG, D, A, B, C,  6,  9, 0xc040b340);
    OP (FG, C, D, A, B, 11, 14, 0x265e5a51);
    OP (FG, B, C, D, A,  0, 20, 0xe9b6c7aa);
    OP (FG, A, B, C, D,  5,  5, 0xd62f105d);
    OP (FG, D, A, B, C, 10,  9, 0x02441453);
    OP (FG, C, D, A, B, 15, 14, 0xd8a1e681);
    OP (FG, B, C, D, A,  4, 20, 0xe7d3fbc8);
    OP (FG, A, B, C, D,  9,  5, 0x21e1cde6);
    OP (FG, D, A, B, C, 14,  9, 0xc33707d6);
    OP (FG, C, D, A, B,  3, 14, 0xf4d50d87);
    OP (FG, B, C, D, A,  8, 20, 0x455a14ed);
    OP (FG, A, B, C, D, 13,  5, 0xa9e3e905);
    OP (FG, D, A, B, C,  2,  9, 0xfcefa3f8);
    OP (FG, C, D, A, B,  7, 14, 0x676f02d9);
    OP (FG, B, C, D, A, 12, 20, 0x8d2a4c8a);

    /* Round 3.  */
    OP (FH, A, B, C, D,  5,  4, 0xfffa3942);
    OP (FH, D, A, B, C,  8, 11, 0x8771f681);
    OP (FH, C, D, A, B, 11, 16, 0x6d9d6122);
    OP (FH, B, C, D, A, 14, 23, 0xfde5380c);
    OP (FH, A, B, C, D,  1,  4, 0xa4beea44);
    OP (FH, D, A, B, C,  4, 11, 0x4bdecfa9);
    OP (FH, C, D, A, B,  7, 16, 0xf6bb4b60);
    OP (FH, B, C, D, A, 10, 23, 0xbebfbc70);
    OP (FH, A, B, C, D, 13,  4, 0x289b7ec6);
    OP (FH, D, A, B, C,  0, 11, 0xeaa127fa);
    OP (FH, C, D, A, B,  3, 16, 0xd4ef3085);
    OP (FH, B, C, D, A,  6, 23, 0x04881d05);
    OP (FH, A, B, C, D,  9,  4, 0xd9d4d039);
    OP (FH, D, A, B, C, 12, 11, 0xe6db99e5);
    OP (FH, C, D, A, B, 15, 16, 0x1fa27cf8);
    OP (FH, B, C, D, A,  2, 23, 0xc4ac5665);

    /* Round 4.  */
    OP (FI, A, B, C, D,  0,  6, 0xf4292244);
    OP (FI, D, A, B, C,  7, 10, 0x432aff97);
    OP (FI, C, D, A, B, 14, 15, 0xab9423a7);
    OP (FI, B, C, D, A,  5, 21, 0xfc93a039);
    OP (FI, A, B, C, D, 12,  6, 0x655b59c3);
    OP (FI, D, A, B, C,  3, 10, 0x8f0ccc92);
    OP (FI, C, D, A, B, 10, 15, 0xffeff47d);
    OP (FI, B, C, D, A,  1, 21, 0x85845dd1);
    OP (FI, A, B, C, D,  8,  6, 0x6fa87e4f);
    OP (FI, D, A, B, C, 15, 10, 0xfe2ce6e0);
    OP (FI, C, D, A, B,  6, 15, 0xa3014314);
    OP (FI, B, C, D, A, 13, 21, 0x4e0811a1);
    OP (FI, A, B, C, D,  4,  6, 0xf7537e82);
    OP (FI, D, A, B, C, 11, 10, 0xbd3af235);
    OP (FI, C, D, A, B,  2, 15, 0x2ad7d2bb);
    OP (FI, B, C, D, A,  9, 21, 0xeb86d391);

    /* Put checksum in context given as argument.  */
    ctx->A += A;
    ctx->B += B;
    ctx->C += C;
    ctx->D += D;
}



/* The routine updates the message-digest context to
 * account for the presence of each of the characters inBuf[0..inLen-1]
 * in the message whose digest is being computed.
 */
static void
md5_write( MD5_CONTEXT *hd, byte *inbuf, size_t inlen)
{
    if( hd->count == 64 ) { /* flush the buffer */
	transform( hd, hd->buf );
        burn_stack (80+6*sizeof(void*));
	hd->count = 0;
	hd->nblocks++;
    }
    if( !inbuf )
	return;
    if( hd->count ) {
	for( ; inlen && hd->count < 64; inlen-- )
	    hd->buf[hd->count++] = *inbuf++;
	md5_write( hd, NULL, 0 );
	if( !inlen )
	    return;
    }
    burn_stack (80+6*sizeof(void*));

    while( inlen >= 64 ) {
	transform( hd, inbuf );
	hd->count = 0;
	hd->nblocks++;
	inlen -= 64;
	inbuf += 64;
    }
    for( ; inlen && hd->count < 64; inlen-- )
	hd->buf[hd->count++] = *inbuf++;

}



/* The routine final terminates the message-digest computation and
 * ends with the desired message digest in mdContext->digest[0...15].
 * The handle is prepared for a new MD5 cycle.
 * Returns 16 bytes representing the digest.
 */

static void
md5_final( MD5_CONTEXT *hd )
{
    u32 t, msb, lsb;
    byte *p;

    md5_write(hd, NULL, 0); /* flush */;

    t = hd->nblocks;
    /* multiply by 64 to make a byte count */
    lsb = t << 6;
    msb = t >> 26;
    /* add the count */
    t = lsb;
    if( (lsb += hd->count) < t )
	msb++;
    /* multiply by 8 to make a bit count */
    t = lsb;
    lsb <<= 3;
    msb <<= 3;
    msb |= t >> 29;

    if( hd->count < 56 ) { /* enough room */
	hd->buf[hd->count++] = 0x80; /* pad */
	while( hd->count < 56 )
	    hd->buf[hd->count++] = 0;  /* pad */
    }
    else { /* need one extra block */
	hd->buf[hd->count++] = 0x80; /* pad character */
	while( hd->count < 64 )
	    hd->buf[hd->count++] = 0;
	md5_write(hd, NULL, 0);  /* flush */;
	memset(hd->buf, 0, 56 ); /* fill next block with zeroes */
    }
    /* append the 64 bit count */
    hd->buf[56] = lsb	   ;
    hd->buf[57] = lsb >>  8;
    hd->buf[58] = lsb >> 16;
    hd->buf[59] = lsb >> 24;
    hd->buf[60] = msb	   ;
    hd->buf[61] = msb >>  8;
    hd->buf[62] = msb >> 16;
    hd->buf[63] = msb >> 24;
    transform( hd, hd->buf );
    burn_stack (80+6*sizeof(void*));

    p = hd->buf;
    #define X(a) do { *p++ = hd->a      ; *p++ = hd->a >> 8;      \
		      *p++ = hd->a >> 16; *p++ = hd->a >> 24; } while(0)
    X(A);
    X(B);
    X(C);
    X(D);
  #undef X

}

static byte *
md5_read( MD5_CONTEXT *hd )
{
    return hd->buf;
}

struct ipmi_authdata_s
{
    void          *info;
    void          *(*mem_alloc)(void *info, int size);
    void          (*mem_free)(void *info, void *data);
    unsigned char data[20];
    unsigned int  datalen;
};

/* External functions for the IPMI authcode algorithms. */
int
ipmi_md5_authcode_initl(const unsigned char *password,
			unsigned int        password_len,
			ipmi_authdata_t     *handle,
			void                *info,
			void                *(*mem_alloc)(void *info, int size),
			void                (*mem_free)(void *info, void *data))
{
    struct ipmi_authdata_s *data;

    if (password_len > 20)
	return EINVAL;

    data = mem_alloc(info, sizeof(*data));
    if (!data)
	return ENOMEM;

    data->info = info;
    data->mem_alloc = mem_alloc;
    data->mem_free = mem_free;

    memcpy(data->data, password, password_len);
    data->datalen = password_len;
    *handle = data;
    return 0;
}

int
ipmi_md5_authcode_init(unsigned char   *password,
		       ipmi_authdata_t *handle,
		       void            *info,
		       void            *(*mem_alloc)(void *info, int size),
		       void            (*mem_free)(void *info, void *data))
{
    return ipmi_md5_authcode_initl(password, 16, handle, info, mem_alloc, mem_free);
}

int
ipmi_md5_authcode_gen(ipmi_authdata_t handle,
		      ipmi_auth_sg_t  data[],
		      void            *output)
{
    MD5_CONTEXT ctx;
    int         i;

    md5_init(&ctx);
    md5_write(&ctx, handle->data, handle->datalen);
    for (i=0; data[i].data != NULL; i++) {
	md5_write(&ctx, data[i].data, data[i].len);
    }
    md5_write(&ctx, handle->data, handle->datalen);
    md5_final(&ctx);
    memcpy(output, md5_read(&ctx), 16);
    return 0;
}

int
ipmi_md5_authcode_check(ipmi_authdata_t handle,
			ipmi_auth_sg_t  data[],
			void            *code)
{
    MD5_CONTEXT ctx;
    int         i;

    md5_init(&ctx);
    md5_write(&ctx, handle->data, handle->datalen);
    for (i=0; data[i].data != NULL; i++) {
	md5_write(&ctx, data[i].data, data[i].len);
    }
    md5_write(&ctx, handle->data, handle->datalen);
    md5_final(&ctx);
    if (memcmp(code, md5_read(&ctx), 16) != 0)
	return EINVAL;
    return 0;
}

void
ipmi_md5_authcode_cleanup(ipmi_authdata_t handle)
{
    if (handle != NULL) {
        memset(handle->data, 0, sizeof(handle->data));
        handle->mem_free(handle->info, handle);
    }
}

/* The stuff below is libgcrypt-specific, and does not apply to IPMI.  The
   stuff above is generic.  Nice separation, thank you :-).
   -Corey Minyard
*/
#if 0
/****************
 * Return some information about the algorithm.  We need algo here to
 * distinguish different flavors of the algorithm.
 * Returns: A pointer to string describing the algorithm or NULL if
 *	    the ALGO is invalid.
 */
static const char *
md5_get_info( int algo, size_t *contextsize,
	       byte **r_asnoid, int *r_asnlen, int *r_mdlen,
	       void (**r_init)( void *c ),
	       void (**r_write)( void *c, byte *buf, size_t nbytes ),
	       void (**r_final)( void *c ),
	       byte *(**r_read)( void *c )
	     )
{
    static byte asn[18] = /* Object ID is 1.2.840.113549.2.5 */
		    { 0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86,0x48,
		      0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00, 0x04, 0x10 };

    if( algo != 1 )
	return NULL;

    *contextsize = sizeof(MD5_CONTEXT);
    *r_asnoid = asn;
    *r_asnlen = DIM(asn);
    *r_mdlen = 16;
    *(void  (**)(MD5_CONTEXT *))r_init		       = md5_init;
    *(void  (**)(MD5_CONTEXT *, byte*, size_t))r_write = md5_write;
    *(void  (**)(MD5_CONTEXT *))r_final 	       = md5_final;
    *(byte *(**)(MD5_CONTEXT *))r_read		       = md5_read;

    return "MD5";
}


#ifndef IS_MODULE
static
#endif
const char * const gnupgext_version = "MD5 ($Revision: 1.4 $)";

static struct {
    int class;
    int version;
    int  value;
    void (*func)(void);
} func_table[] = {
    { 10, 1, 0, (void(*)(void))md5_get_info },
    { 11, 1, 1 },
};


#ifndef IS_MODULE
static
#endif
void *
gnupgext_enum_func( int what, int *sequence, int *class, int *vers )
{
    void *ret;
    int i = *sequence;

    do {
	if( i >= DIM(func_table) || i < 0 )
	    return NULL;
	*class = func_table[i].class;
	*vers  = func_table[i].version;
	switch( *class ) {
	  case 11: case 21: case 31: ret = &func_table[i].value; break;
	  default:		     ret = func_table[i].func; break;
	}
	i++;
    } while( what && what != *class );

    *sequence = i;
    return ret;
}




#ifndef IS_MODULE
void
_gcry_md5_constructor(void)
{
    _gcry_register_internal_cipher_extension( gnupgext_version, gnupgext_enum_func );
}
#endif
#endif
/* end of file */
