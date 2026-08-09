/*
 * DEFLATE compressor (RFC 1951): greedy LZ77 with hash chains, encoded as a
 * single fixed-Huffman block. Prioritizes simplicity and universal
 * decodability over maximum ratio.
 * By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define WINSIZE  32768
#define WINMASK  (WINSIZE - 1)
#define HASHSIZE 32768
#define HASHMASK (HASHSIZE - 1)
#define MINMATCH 3
#define MAXMATCH 258
#define MAXCHAIN 128

struct bitwriter
{
    unsigned char *buf;
    size_t cap;
    size_t len;
    unsigned int bitbuf;
    int bitcnt;
    int oom;
};

static void bw_byte(struct bitwriter *w, unsigned char b)
{
    if (w->len >= w->cap)
    {
        size_t ncap = w->cap ? w->cap * 2 : 4096;
        unsigned char *nbuf = (unsigned char *)realloc(w->buf, ncap);
        if (!nbuf)
        {
            w->oom = 1;
            return;
        }
        w->buf = nbuf;
        w->cap = ncap;
    }
    w->buf[w->len++] = b;
}

/* Append `count` bits of `value`, least significant bit first. */
static void bw_bits(struct bitwriter *w, unsigned int value, int count)
{
    w->bitbuf |= (value & ((1u << count) - 1)) << w->bitcnt;
    w->bitcnt += count;
    while (w->bitcnt >= 8)
    {
        bw_byte(w, (unsigned char)(w->bitbuf & 0xFF));
        w->bitbuf >>= 8;
        w->bitcnt -= 8;
    }
}

/* Append a Huffman code: most significant bit of the code goes first. */
static void bw_code(struct bitwriter *w, unsigned int code, int count)
{
    for (int i = count - 1; i >= 0; i--)
        bw_bits(w, (code >> i) & 1u, 1);
}

static void bw_flush(struct bitwriter *w)
{
    if (w->bitcnt > 0)
    {
        bw_byte(w, (unsigned char)(w->bitbuf & 0xFF));
        w->bitbuf = 0;
        w->bitcnt = 0;
    }
}

/* Fixed Huffman literal/length codes (RFC 1951 section 3.2.6) */
static void put_literal(struct bitwriter *w, int sym)
{
    if (sym < 144)
        bw_code(w, 0x30u + (unsigned int)sym, 8);
    else if (sym < 256)
        bw_code(w, 0x190u + (unsigned int)(sym - 144), 9);
    else if (sym < 280)
        bw_code(w, (unsigned int)(sym - 256), 7);
    else
        bw_code(w, 0xC0u + (unsigned int)(sym - 280), 8);
}

static void put_length(struct bitwriter *w, int len)
{
    static const short base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    int i = 28;
    while (i > 0 && base[i] > len)
        i--;
    /* codes 257..284 cover ranges; 285 is exactly 258 */
    if (i < 28 && base[i + 1] <= len)
        i++;
    put_literal(w, 257 + i);
    if (extra[i])
        bw_bits(w, (unsigned int)(len - base[i]), extra[i]);
}

static void put_distance(struct bitwriter *w, int dist)
{
    static const short base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    int i = 29;
    while (i > 0 && base[i] > dist)
        i--;
    bw_code(w, (unsigned int)i, 5); /* fixed distance codes are 5 bits */
    if (extra[i])
        bw_bits(w, (unsigned int)(dist - base[i]), extra[i]);
}

static unsigned int hash3(const unsigned char *p)
{
    return (((unsigned int)p[0] << 10) ^ ((unsigned int)p[1] << 5) ^ (unsigned int)p[2]) & HASHMASK;
}

/* Compress src into a malloc'd buffer returned to the caller (free() it).
 * Returns NULL on allocation failure. *outlen receives compressed size. */
unsigned char *smn_deflate(const unsigned char *src, size_t srclen, size_t *outlen)
{
    struct bitwriter w = {0};
    long *head = NULL, *prev = NULL;

    head = (long *)malloc(HASHSIZE * sizeof(long));
    prev = (long *)malloc(WINSIZE * sizeof(long));
    if (!head || !prev)
        goto fail;
    for (int i = 0; i < HASHSIZE; i++)
        head[i] = -1;

    bw_bits(&w, 1, 1); /* BFINAL */
    bw_bits(&w, 1, 2); /* BTYPE = 01 fixed Huffman */

    size_t pos = 0;
    while (pos < srclen)
    {
        int bestlen = 0;
        long bestdist = 0;

        if (pos + MINMATCH <= srclen)
        {
            unsigned int h = hash3(src + pos);
            long cand = head[h];
            int chain = MAXCHAIN;
            size_t limit = (srclen - pos < MAXMATCH) ? srclen - pos : MAXMATCH;

            while (cand >= 0 && chain-- > 0 && pos - (size_t)cand <= WINSIZE)
            {
                const unsigned char *a = src + pos;
                const unsigned char *b = src + cand;
                size_t len = 0;
                while (len < limit && a[len] == b[len])
                    len++;
                if ((int)len > bestlen)
                {
                    bestlen = (int)len;
                    bestdist = (long)(pos - (size_t)cand);
                    if (len == limit)
                        break;
                }
                cand = prev[(size_t)cand & WINMASK];
            }
        }

        if (bestlen >= MINMATCH)
        {
            put_length(&w, bestlen);
            put_distance(&w, (int)bestdist);
            /* advance past the match, inserting covered positions into the
             * hash chains while a full 3-byte sequence remains */
            for (int i = 0; i < bestlen; i++, pos++)
            {
                if (pos + MINMATCH <= srclen)
                {
                    unsigned int h = hash3(src + pos);
                    prev[pos & WINMASK] = head[h];
                    head[h] = (long)pos;
                }
            }
        }
        else
        {
            put_literal(&w, src[pos]);
            if (pos + MINMATCH <= srclen)
            {
                unsigned int h = hash3(src + pos);
                prev[pos & WINMASK] = head[h];
                head[h] = (long)pos;
            }
            pos++;
        }
    }

    put_literal(&w, 256); /* end of block */
    bw_flush(&w);

    free(head);
    free(prev);

    if (w.oom || w.buf == NULL)
    {
        free(w.buf);
        return NULL;
    }
    *outlen = w.len;
    return w.buf;

fail:
    free(head);
    free(prev);
    free(w.buf);
    return NULL;
}
