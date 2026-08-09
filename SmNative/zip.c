/*
 * Zip archive reader/writer covering the subset of features SoundManager
 * needs: stored + deflate methods, CRC-32 verification, UTF-8 and CP437
 * entry names. No zip64, no encryption, no multi-disk.
 * By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
 */
#ifndef SMNATIVE_BUILD
#define SMNATIVE_BUILD
#endif
#include "smnative.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIG_LOCAL   0x04034b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_EOCD    0x06054b50u
#define FLAG_UTF8   0x0800u
#define FLAG_CRYPT  0x0001u

static unsigned int rd16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int rd32(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }

static int wr16(FILE *f, unsigned int v)
{
    unsigned char b[2] = {(unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF)};
    return fwrite(b, 1, 2, f) == 2 ? 0 : SMN_E_IO;
}

static int wr32(FILE *f, unsigned int v)
{
    unsigned char b[4] = {(unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
                          (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF)};
    return fwrite(b, 1, 4, f) == 4 ? 0 : SMN_E_IO;
}

/* ------------------------------------------------------------------ */
/* Reader                                                             */
/* ------------------------------------------------------------------ */

struct zentry
{
    wchar_t *name;
    unsigned int flags;
    unsigned int method;
    unsigned int crc;
    unsigned long long csize;
    unsigned long long usize;
    unsigned long long lho; /* local header offset */
};

struct smzip_reader
{
    FILE *f;
    struct zentry *entries;
    int count;
};

static wchar_t *name_to_wide(const unsigned char *bytes, int len, unsigned int flags)
{
    UINT cp = (flags & FLAG_UTF8) ? CP_UTF8 : 437; /* IBM437: zip legacy default */
    int wlen = MultiByteToWideChar(cp, 0, (const char *)bytes, len, NULL, 0);
    if (wlen <= 0 && len > 0)
        return NULL;
    wchar_t *out = (wchar_t *)malloc(((size_t)wlen + 1) * sizeof(wchar_t));
    if (!out)
        return NULL;
    if (wlen > 0)
        MultiByteToWideChar(cp, 0, (const char *)bytes, len, out, wlen);
    out[wlen] = L'\0';
    return out;
}

void smzip_close(smzip_reader *r)
{
    if (!r)
        return;
    if (r->f)
        fclose(r->f);
    if (r->entries)
    {
        for (int i = 0; i < r->count; i++)
            free(r->entries[i].name);
        free(r->entries);
    }
    free(r);
}

int smzip_open(const wchar_t *path, smzip_reader **out)
{
    if (!path || !out)
        return SMN_E_ARG;
    *out = NULL;

    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return SMN_E_IO;

    int err = SMN_E_FORMAT;
    unsigned char *tail = NULL, *cd = NULL;
    smzip_reader *r = NULL;

    if (_fseeki64(f, 0, SEEK_END) != 0)
        goto io_fail;
    long long fsize = _ftelli64(f);
    if (fsize < 22)
        goto fail; /* smaller than an empty zip */

    /* EOCD is within the last 65557 bytes (max comment) + 22 */
    long long tailsize = fsize < 65579 ? fsize : 65579;
    tail = (unsigned char *)malloc((size_t)tailsize);
    if (!tail)
    {
        err = SMN_E_MEM;
        goto fail;
    }
    if (_fseeki64(f, fsize - tailsize, SEEK_SET) != 0 || fread(tail, 1, (size_t)tailsize, f) != (size_t)tailsize)
        goto io_fail;

    long long eocd = -1;
    for (long long i = tailsize - 22; i >= 0; i--)
    {
        if (rd32(tail + i) == SIG_EOCD)
        {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        goto fail;

    unsigned int count = rd16(tail + eocd + 10);
    unsigned int cdsize = rd32(tail + eocd + 12);
    unsigned int cdoffset = rd32(tail + eocd + 16);
    if (count == 0xFFFF || cdsize == 0xFFFFFFFFu || cdoffset == 0xFFFFFFFFu)
    {
        err = SMN_E_UNSUPPORTED; /* zip64 */
        goto fail;
    }
    if ((long long)cdoffset + cdsize > fsize)
        goto fail;

    cd = (unsigned char *)malloc(cdsize ? cdsize : 1);
    if (!cd)
    {
        err = SMN_E_MEM;
        goto fail;
    }
    if (_fseeki64(f, cdoffset, SEEK_SET) != 0 || fread(cd, 1, cdsize, f) != cdsize)
        goto io_fail;

    r = (smzip_reader *)calloc(1, sizeof(smzip_reader));
    if (!r)
    {
        err = SMN_E_MEM;
        goto fail;
    }
    r->f = f;
    r->entries = (struct zentry *)calloc(count ? count : 1, sizeof(struct zentry));
    if (!r->entries)
    {
        err = SMN_E_MEM;
        goto fail;
    }

    size_t pos = 0;
    for (unsigned int i = 0; i < count; i++)
    {
        if (pos + 46 > cdsize || rd32(cd + pos) != SIG_CENTRAL)
            goto fail;
        struct zentry *e = &r->entries[r->count];
        e->flags = rd16(cd + pos + 8);
        e->method = rd16(cd + pos + 10);
        e->crc = rd32(cd + pos + 16);
        e->csize = rd32(cd + pos + 20);
        e->usize = rd32(cd + pos + 24);
        unsigned int namelen = rd16(cd + pos + 28);
        unsigned int extralen = rd16(cd + pos + 30);
        unsigned int commentlen = rd16(cd + pos + 32);
        e->lho = rd32(cd + pos + 42);
        if (pos + 46 + namelen > cdsize)
            goto fail;
        if (e->csize == 0xFFFFFFFFu || e->usize == 0xFFFFFFFFu || e->lho == 0xFFFFFFFFu)
        {
            err = SMN_E_UNSUPPORTED; /* zip64 */
            goto fail;
        }
        e->name = name_to_wide(cd + pos + 46, (int)namelen, e->flags);
        if (!e->name)
        {
            err = SMN_E_MEM;
            goto fail;
        }
        r->count++;
        pos += 46 + namelen + extralen + commentlen;
    }

    free(tail);
    free(cd);
    *out = r;
    return SMN_OK;

io_fail:
    err = SMN_E_IO;
fail:
    free(tail);
    free(cd);
    if (r)
    {
        r->f = NULL; /* still owned by this function */
        smzip_close(r);
    }
    fclose(f);
    return err;
}

int smzip_entry_count(smzip_reader *r)
{
    return r ? r->count : SMN_E_ARG;
}

int smzip_entry_name(smzip_reader *r, int index, wchar_t *buf, int bufchars)
{
    if (!r || index < 0 || index >= r->count)
        return SMN_E_NOTFOUND;
    int need = (int)wcslen(r->entries[index].name);
    if (buf && bufchars > 0)
    {
        int copy = need < bufchars - 1 ? need : bufchars - 1;
        memcpy(buf, r->entries[index].name, (size_t)copy * sizeof(wchar_t));
        buf[copy] = L'\0';
    }
    return need;
}

long long smzip_entry_size(smzip_reader *r, int index)
{
    if (!r || index < 0 || index >= r->count)
        return SMN_E_NOTFOUND;
    return (long long)r->entries[index].usize;
}

/* Read and decompress entry data. Returns malloc'd buffer of usize bytes. */
static int extract_entry(smzip_reader *r, int index, unsigned char **outbuf)
{
    struct zentry *e = &r->entries[index];
    if (e->flags & FLAG_CRYPT)
        return SMN_E_UNSUPPORTED;
    if (e->method != 0 && e->method != 8)
        return SMN_E_UNSUPPORTED;

    unsigned char lh[30];
    if (_fseeki64(r->f, (long long)e->lho, SEEK_SET) != 0 || fread(lh, 1, 30, r->f) != 30)
        return SMN_E_IO;
    if (rd32(lh) != SIG_LOCAL)
        return SMN_E_FORMAT;
    unsigned int namelen = rd16(lh + 26);
    unsigned int extralen = rd16(lh + 28);
    if (_fseeki64(r->f, (long long)e->lho + 30 + namelen + extralen, SEEK_SET) != 0)
        return SMN_E_IO;

    unsigned char *cdata = (unsigned char *)malloc(e->csize ? (size_t)e->csize : 1);
    if (!cdata)
        return SMN_E_MEM;
    if (fread(cdata, 1, (size_t)e->csize, r->f) != (size_t)e->csize)
    {
        free(cdata);
        return SMN_E_IO;
    }

    unsigned char *udata;
    if (e->method == 0)
    {
        if (e->csize != e->usize)
        {
            free(cdata);
            return SMN_E_FORMAT;
        }
        udata = cdata;
    }
    else
    {
        udata = (unsigned char *)malloc(e->usize ? (size_t)e->usize : 1);
        if (!udata)
        {
            free(cdata);
            return SMN_E_MEM;
        }
        int ierr = smn_inflate(cdata, (size_t)e->csize, udata, (size_t)e->usize);
        free(cdata);
        if (ierr != 0)
        {
            free(udata);
            return SMN_E_FORMAT;
        }
    }

    if (smn_crc32(0, udata, (size_t)e->usize) != e->crc)
    {
        free(udata);
        return SMN_E_CRC;
    }

    *outbuf = udata;
    return SMN_OK;
}

int smzip_extract_to_memory(smzip_reader *r, int index, unsigned char *buf, long long bufsize)
{
    if (!r || index < 0 || index >= r->count)
        return SMN_E_NOTFOUND;
    if (!buf && r->entries[index].usize > 0)
        return SMN_E_ARG;
    if (bufsize < (long long)r->entries[index].usize)
        return SMN_E_ARG;

    unsigned char *data;
    int err = extract_entry(r, index, &data);
    if (err != SMN_OK)
        return err;
    memcpy(buf, data, (size_t)r->entries[index].usize);
    free(data);
    return SMN_OK;
}

int smzip_extract_to_file(smzip_reader *r, int index, const wchar_t *outPath)
{
    if (!r || index < 0 || index >= r->count)
        return SMN_E_NOTFOUND;
    if (!outPath)
        return SMN_E_ARG;

    unsigned char *data;
    int err = extract_entry(r, index, &data);
    if (err != SMN_OK)
        return err;

    FILE *out = _wfopen(outPath, L"wb");
    if (!out)
    {
        free(data);
        return SMN_E_IO;
    }
    size_t usize = (size_t)r->entries[index].usize;
    size_t written = usize ? fwrite(data, 1, usize, out) : 0;
    free(data);
    if (fclose(out) != 0 || written != usize)
        return SMN_E_IO;
    return SMN_OK;
}

int smzip_check(const wchar_t *path)
{
    smzip_reader *r;
    int err = smzip_open(path, &r);
    if (err != SMN_OK)
        return err;

    /* verify each entry has a valid local header and supported method */
    for (int i = 0; i < r->count; i++)
    {
        struct zentry *e = &r->entries[i];
        unsigned char lh[30];
        if (_fseeki64(r->f, (long long)e->lho, SEEK_SET) != 0 || fread(lh, 1, 30, r->f) != 30 || rd32(lh) != SIG_LOCAL)
        {
            err = SMN_E_FORMAT;
            break;
        }
        if ((e->flags & FLAG_CRYPT) || (e->method != 0 && e->method != 8))
        {
            err = SMN_E_UNSUPPORTED;
            break;
        }
    }
    smzip_close(r);
    return err;
}

/* ------------------------------------------------------------------ */
/* Writer                                                             */
/* ------------------------------------------------------------------ */

struct wentry
{
    unsigned char *nameBytes;
    unsigned int nameLen;
    unsigned int flags;
    unsigned int method;
    unsigned int dostime;
    unsigned int dosdate;
    unsigned int crc;
    unsigned long long csize;
    unsigned long long usize;
    unsigned long long lho;
};

struct smzip_writer
{
    FILE *f;
    wchar_t *path;
    struct wentry *entries;
    int count;
    int cap;
    unsigned long long offset;
};

static void current_dos_time(unsigned int *dostime, unsigned int *dosdate)
{
    SYSTEMTIME st;
    FILETIME ft, lft;
    WORD fatdate = 0, fattime = 0;
    GetLocalTime(&st);
    if (SystemTimeToFileTime(&st, &ft) && FileTimeToDosDateTime(&ft, &fatdate, &fattime))
    {
        *dosdate = fatdate;
        *dostime = fattime;
    }
    else
    {
        *dosdate = 0x21; /* 1980-01-01 */
        *dostime = 0;
    }
    (void)lft;
}

static void file_dos_time(const wchar_t *path, unsigned int *dostime, unsigned int *dosdate)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    FILETIME lft;
    WORD fatdate = 0, fattime = 0;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)
        && FileTimeToLocalFileTime(&fad.ftLastWriteTime, &lft)
        && FileTimeToDosDateTime(&lft, &fatdate, &fattime))
    {
        *dosdate = fatdate;
        *dostime = fattime;
    }
    else
        current_dos_time(dostime, dosdate);
}

/* Encode entry name: plain ASCII when possible, UTF-8 (flag bit 11) otherwise. */
static int encode_name(const wchar_t *name, unsigned char **bytes, unsigned int *len, unsigned int *flags)
{
    int ascii = 1;
    for (const wchar_t *p = name; *p; p++)
        if (*p >= 0x80)
        {
            ascii = 0;
            break;
        }
    int blen = WideCharToMultiByte(CP_UTF8, 0, name, -1, NULL, 0, NULL, NULL);
    if (blen <= 1)
        return SMN_E_ARG;
    unsigned char *buf = (unsigned char *)malloc((size_t)blen);
    if (!buf)
        return SMN_E_MEM;
    WideCharToMultiByte(CP_UTF8, 0, name, -1, (char *)buf, blen, NULL, NULL);
    *bytes = buf;
    *len = (unsigned int)(blen - 1); /* exclude NUL */
    *flags = ascii ? 0 : FLAG_UTF8;
    return SMN_OK;
}

int smzipw_create(const wchar_t *path, smzip_writer **out)
{
    if (!path || !out)
        return SMN_E_ARG;
    *out = NULL;

    smzip_writer *w = (smzip_writer *)calloc(1, sizeof(smzip_writer));
    if (!w)
        return SMN_E_MEM;
    w->f = _wfopen(path, L"wb");
    if (!w->f)
    {
        free(w);
        return SMN_E_IO;
    }
    w->path = _wcsdup(path);
    if (!w->path)
    {
        fclose(w->f);
        free(w);
        return SMN_E_MEM;
    }
    *out = w;
    return SMN_OK;
}

static int add_common(smzip_writer *w, const wchar_t *entryName, const unsigned char *data,
                      unsigned long long size, unsigned int dostime, unsigned int dosdate)
{
    if (!w || !entryName || (size > 0 && !data))
        return SMN_E_ARG;
    if (size > 0xFFFFFFF0ull || w->offset > 0xFFFFFFF0ull)
        return SMN_E_UNSUPPORTED; /* would need zip64 */

    unsigned char *nameBytes;
    unsigned int nameLen, flags;
    int err = encode_name(entryName, &nameBytes, &nameLen, &flags);
    if (err != SMN_OK)
        return err;

    unsigned int crc = smn_crc32(0, data, (size_t)size);

    /* compress; fall back to stored when not smaller */
    unsigned int method = 8;
    size_t csize = 0;
    unsigned char *cdata = smn_deflate(data, (size_t)size, &csize);
    if (!cdata)
    {
        free(nameBytes);
        return SMN_E_MEM;
    }
    const unsigned char *payload = cdata;
    if (csize >= size)
    {
        method = 0;
        payload = data;
        csize = (size_t)size;
    }

    if (w->count == w->cap)
    {
        int ncap = w->cap ? w->cap * 2 : 16;
        struct wentry *ne = (struct wentry *)realloc(w->entries, (size_t)ncap * sizeof(struct wentry));
        if (!ne)
        {
            free(nameBytes);
            free(cdata);
            return SMN_E_MEM;
        }
        w->entries = ne;
        w->cap = ncap;
    }

    err = SMN_E_IO;
    if (wr32(w->f, SIG_LOCAL) || wr16(w->f, 20) || wr16(w->f, flags) || wr16(w->f, method)
        || wr16(w->f, dostime) || wr16(w->f, dosdate) || wr32(w->f, crc)
        || wr32(w->f, (unsigned int)csize) || wr32(w->f, (unsigned int)size)
        || wr16(w->f, nameLen) || wr16(w->f, 0))
        goto done;
    if (fwrite(nameBytes, 1, nameLen, w->f) != nameLen)
        goto done;
    if (csize > 0 && fwrite(payload, 1, csize, w->f) != csize)
        goto done;

    {
        struct wentry *e = &w->entries[w->count];
        e->nameBytes = nameBytes;
        e->nameLen = nameLen;
        e->flags = flags;
        e->method = method;
        e->dostime = dostime;
        e->dosdate = dosdate;
        e->crc = crc;
        e->csize = csize;
        e->usize = size;
        e->lho = w->offset;
        w->count++;
    }
    w->offset += 30 + nameLen + csize;
    nameBytes = NULL; /* ownership moved to entry */
    err = SMN_OK;

done:
    free(nameBytes);
    free(cdata);
    return err;
}

int smzipw_add_data(smzip_writer *w, const wchar_t *entryName, const unsigned char *data, long long size)
{
    if (size < 0)
        return SMN_E_ARG;
    unsigned int dostime, dosdate;
    current_dos_time(&dostime, &dosdate);
    return add_common(w, entryName, data, (unsigned long long)size, dostime, dosdate);
}

int smzipw_add_file(smzip_writer *w, const wchar_t *entryName, const wchar_t *srcPath)
{
    if (!w || !entryName || !srcPath)
        return SMN_E_ARG;

    FILE *f = _wfopen(srcPath, L"rb");
    if (!f)
        return SMN_E_IO;
    if (_fseeki64(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return SMN_E_IO;
    }
    long long size = _ftelli64(f);
    if (size < 0 || _fseeki64(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return SMN_E_IO;
    }
    unsigned char *data = (unsigned char *)malloc(size ? (size_t)size : 1);
    if (!data)
    {
        fclose(f);
        return SMN_E_MEM;
    }
    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        fclose(f);
        return SMN_E_IO;
    }
    fclose(f);

    unsigned int dostime, dosdate;
    file_dos_time(srcPath, &dostime, &dosdate);
    int err = add_common(w, entryName, data, (unsigned long long)size, dostime, dosdate);
    free(data);
    return err;
}

int smzipw_close(smzip_writer *w, int discard)
{
    if (!w)
        return SMN_E_ARG;

    int err = SMN_OK;
    if (!discard)
    {
        unsigned long long cdstart = w->offset;
        for (int i = 0; i < w->count && err == SMN_OK; i++)
        {
            struct wentry *e = &w->entries[i];
            if (wr32(w->f, SIG_CENTRAL) || wr16(w->f, 20) || wr16(w->f, 20)
                || wr16(w->f, e->flags) || wr16(w->f, e->method)
                || wr16(w->f, e->dostime) || wr16(w->f, e->dosdate)
                || wr32(w->f, e->crc) || wr32(w->f, (unsigned int)e->csize)
                || wr32(w->f, (unsigned int)e->usize) || wr16(w->f, e->nameLen)
                || wr16(w->f, 0) || wr16(w->f, 0) || wr16(w->f, 0) || wr16(w->f, 0)
                || wr32(w->f, 0x20 /* archive attribute */) || wr32(w->f, (unsigned int)e->lho))
                err = SMN_E_IO;
            else if (fwrite(e->nameBytes, 1, e->nameLen, w->f) != e->nameLen)
                err = SMN_E_IO;
            else
                w->offset += 46 + e->nameLen;
        }
        if (err == SMN_OK)
        {
            unsigned long long cdsize = w->offset - cdstart;
            if (wr32(w->f, SIG_EOCD) || wr16(w->f, 0) || wr16(w->f, 0)
                || wr16(w->f, (unsigned int)w->count) || wr16(w->f, (unsigned int)w->count)
                || wr32(w->f, (unsigned int)cdsize) || wr32(w->f, (unsigned int)cdstart)
                || wr16(w->f, 0))
                err = SMN_E_IO;
        }
    }

    if (fclose(w->f) != 0 && err == SMN_OK)
        err = SMN_E_IO;
    if (discard)
        _wremove(w->path);

    for (int i = 0; i < w->count; i++)
        free(w->entries[i].nameBytes);
    free(w->entries);
    free(w->path);
    free(w);
    return err;
}
