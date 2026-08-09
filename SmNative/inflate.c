/*
 * DEFLATE decompressor (RFC 1951): stored, fixed and dynamic Huffman blocks.
 * Operates on whole buffers; sizes are known in advance from zip metadata.
 * By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
 */
#include <stddef.h>
#include <string.h>

#define MAXBITS   15  /* maximum bits in a Huffman code */
#define MAXLCODES 288 /* literal/length codes */
#define MAXDCODES 30  /* distance codes */
#define MAXCODES  (MAXLCODES + MAXDCODES)

struct instate
{
    const unsigned char *in;
    size_t inlen;
    size_t inpos;
    unsigned char *out;
    size_t outlen;
    size_t outpos;
    unsigned int bitbuf;
    int bitcnt;
};

struct huffman
{
    short count[MAXBITS + 1]; /* number of codes of each length */
    short symbol[MAXCODES];   /* sorted symbols */
};

/* Return `need` bits from the stream, LSB first. Negative on exhaustion. */
static int bits(struct instate *s, int need)
{
    unsigned int val = s->bitbuf;
    while (s->bitcnt < need)
    {
        if (s->inpos >= s->inlen)
            return -1;
        val |= (unsigned int)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

/* Canonical Huffman decode, one bit at a time over code lengths 1..MAXBITS. */
static int decode(struct instate *s, const struct huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++)
    {
        int bit = bits(s, 1);
        if (bit < 0)
            return -9;
        code |= bit;
        int count = h->count[len];
        if (code - first < count)
            return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -10; /* ran out of codes */
}

/* Build Huffman decoding tables from a list of code lengths.
 * Returns 0 if complete, positive if incomplete (usable), negative on error. */
static int construct(struct huffman *h, const short *length, int n)
{
    short offs[MAXBITS + 1];

    for (int len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (int symbol = 0; symbol < n; symbol++)
        h->count[length[symbol]]++;
    if (h->count[0] == n)
        return 0; /* no codes at all */

    int left = 1;
    for (int len = 1; len <= MAXBITS; len++)
    {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1; /* over-subscribed */
    }

    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);

    for (int symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0)
            h->symbol[offs[length[symbol]]++] = (short)symbol;

    return left; /* zero for complete set, positive for incomplete */
}

/* Decode literal/length and distance codes until end-of-block. */
static int codes(struct instate *s, const struct huffman *lencode, const struct huffman *distcode)
{
    static const short lens[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    int symbol;
    do
    {
        symbol = decode(s, lencode);
        if (symbol < 0)
            return symbol;
        if (symbol < 256)
        {
            if (s->outpos >= s->outlen)
                return -11; /* output overflow */
            s->out[s->outpos++] = (unsigned char)symbol;
        }
        else if (symbol > 256)
        {
            symbol -= 257;
            if (symbol >= 29)
                return -12;
            int extra = bits(s, lext[symbol]);
            if (extra < 0)
                return -9;
            int len = lens[symbol] + extra;

            symbol = decode(s, distcode);
            if (symbol < 0)
                return symbol;
            if (symbol >= 30)
                return -13;
            extra = bits(s, dext[symbol]);
            if (extra < 0)
                return -9;
            size_t dist = (size_t)dists[symbol] + (size_t)extra;
            if (dist > s->outpos)
                return -14; /* distance too far back */
            if (s->outpos + (size_t)len > s->outlen)
                return -11;
            /* byte-by-byte copy handles overlapping matches correctly */
            for (int i = 0; i < len; i++)
            {
                s->out[s->outpos] = s->out[s->outpos - dist];
                s->outpos++;
            }
        }
    } while (symbol != 256);
    return 0;
}

static int fixed_tables(struct huffman *lencode, struct huffman *distcode)
{
    short lengths[MAXCODES];
    int symbol;

    for (symbol = 0; symbol < 144; symbol++)
        lengths[symbol] = 8;
    for (; symbol < 256; symbol++)
        lengths[symbol] = 9;
    for (; symbol < 280; symbol++)
        lengths[symbol] = 7;
    for (; symbol < MAXLCODES; symbol++)
        lengths[symbol] = 8;
    if (construct(lencode, lengths, MAXLCODES) < 0)
        return -15;

    for (symbol = 0; symbol < MAXDCODES; symbol++)
        lengths[symbol] = 5;
    if (construct(distcode, lengths, MAXDCODES) < 0)
        return -15;

    return 0;
}

static int stored(struct instate *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;

    if (s->inpos + 4 > s->inlen)
        return -2;
    unsigned int len = (unsigned int)s->in[s->inpos] | ((unsigned int)s->in[s->inpos + 1] << 8);
    unsigned int nlen = (unsigned int)s->in[s->inpos + 2] | ((unsigned int)s->in[s->inpos + 3] << 8);
    s->inpos += 4;
    if ((len ^ 0xFFFF) != nlen)
        return -2;
    if (s->inpos + len > s->inlen)
        return -2;
    if (s->outpos + len > s->outlen)
        return -11;
    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len;
    s->outpos += len;
    return 0;
}

static int dynamic(struct instate *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[MAXCODES];
    struct huffman lencode, distcode;

    int nlen = bits(s, 5);
    int ndist = bits(s, 5);
    int ncode = bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0)
        return -9;
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES)
        return -3;

    for (int index = 0; index < ncode; index++)
    {
        int b = bits(s, 3);
        if (b < 0)
            return -9;
        lengths[order[index]] = (short)b;
    }
    for (int index = ncode; index < 19; index++)
        lengths[order[index]] = 0;

    if (construct(&lencode, lengths, 19) != 0)
        return -4; /* code length codes must be complete */

    int index = 0;
    while (index < nlen + ndist)
    {
        int symbol = decode(s, &lencode);
        if (symbol < 0)
            return symbol;
        if (symbol < 16)
            lengths[index++] = (short)symbol;
        else
        {
            int len = 0; /* value to repeat */
            int count;
            if (symbol == 16)
            {
                if (index == 0)
                    return -5;
                len = lengths[index - 1];
                count = bits(s, 2);
                if (count < 0)
                    return -9;
                count += 3;
            }
            else if (symbol == 17)
            {
                count = bits(s, 3);
                if (count < 0)
                    return -9;
                count += 3;
            }
            else
            {
                count = bits(s, 7);
                if (count < 0)
                    return -9;
                count += 11;
            }
            if (index + count > nlen + ndist)
                return -6;
            while (count--)
                lengths[index++] = (short)len;
        }
    }

    if (lengths[256] == 0)
        return -9; /* no end-of-block code */

    int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
        return -7;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
        return -8;

    return codes(s, &lencode, &distcode);
}

/* Inflate src into dst. dst must be exactly the uncompressed size.
 * Returns 0 on success, negative on corrupt or unsupported data. */
int smn_inflate(const unsigned char *src, size_t srclen, unsigned char *dst, size_t dstlen)
{
    struct instate s;
    s.in = src;
    s.inlen = srclen;
    s.inpos = 0;
    s.out = dst;
    s.outlen = dstlen;
    s.outpos = 0;
    s.bitbuf = 0;
    s.bitcnt = 0;

    int last, type, err;
    do
    {
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (last < 0 || type < 0)
            return -9;
        if (type == 0)
            err = stored(&s);
        else if (type == 1)
        {
            struct huffman lencode, distcode;
            err = fixed_tables(&lencode, &distcode);
            if (err == 0)
                err = codes(&s, &lencode, &distcode);
        }
        else if (type == 2)
            err = dynamic(&s);
        else
            return -1; /* reserved block type */
        if (err != 0)
            return err;
    } while (!last);

    return (s.outpos == s.outlen) ? 0 : -16; /* must fill output exactly */
}
