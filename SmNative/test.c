/*
 * Test harness for SmNative. Not shipped with the application.
 *   test selftest <tmpdir>       run zip round-trip + audio transcode tests
 *   test create <zip> <file>...  create zip (for cross-checking with other tools)
 *   test extract <zip> <outdir>  extract all entries (flat names only)
 *   test list <zip>              list entries
 * By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
 */
#include "smnative.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

static int failures = 0;

static void check(int cond, const char *what)
{
    if (cond)
        printf("  OK  %s\n", what);
    else
    {
        printf("FAIL  %s\n", what);
        failures++;
    }
}

static wchar_t *path_join(const wchar_t *dir, const wchar_t *name)
{
    static wchar_t buf[8][1024];
    static int slot = 0;
    wchar_t *out = buf[slot++ & 7];
    _snwprintf(out, 1024, L"%s\\%s", dir, name);
    out[1023] = L'\0';
    return out;
}

/* --- data generators --- */

static unsigned char *gen_text(size_t n)
{
    const char *pat = "SoundManager test pattern - the quick brown fox jumps over the lazy dog. ";
    size_t plen = strlen(pat);
    unsigned char *b = (unsigned char *)malloc(n);
    for (size_t i = 0; i < n; i++)
        b[i] = (unsigned char)pat[i % plen];
    return b;
}

static unsigned char *gen_random(size_t n, unsigned int seed)
{
    unsigned char *b = (unsigned char *)malloc(n);
    unsigned int s = seed;
    for (size_t i = 0; i < n; i++)
    {
        s = s * 1103515245u + 12345u;
        b[i] = (unsigned char)(s >> 16);
    }
    return b;
}

static int write_file(const wchar_t *path, const unsigned char *data, size_t n)
{
    FILE *f = _wfopen(path, L"wb");
    if (!f)
        return -1;
    size_t w = n ? fwrite(data, 1, n, f) : 0;
    fclose(f);
    return w == n ? 0 : -1;
}

static unsigned char *read_file(const wchar_t *path, size_t *n)
{
    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = (unsigned char *)malloc(sz > 0 ? (size_t)sz : 1);
    if (sz > 0 && fread(b, 1, (size_t)sz, f) != (size_t)sz)
    {
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *n = (size_t)sz;
    return b;
}

/* 1 second 440 Hz sine, 44100 Hz, 16-bit, stereo */
static int gen_wav(const wchar_t *path)
{
    const int rate = 44100, ch = 2, secs = 1;
    const int frames = rate * secs;
    unsigned int dataSize = (unsigned int)(frames * ch * 2);
    unsigned char *buf = (unsigned char *)malloc(44 + dataSize);
    unsigned int riff = 36 + dataSize, avg = (unsigned int)(rate * ch * 2);
    memcpy(buf, "RIFF", 4);
    buf[4] = (unsigned char)riff; buf[5] = (unsigned char)(riff >> 8); buf[6] = (unsigned char)(riff >> 16); buf[7] = (unsigned char)(riff >> 24);
    memcpy(buf + 8, "WAVEfmt ", 8);
    buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0;
    buf[20] = 1; buf[21] = 0;
    buf[22] = (unsigned char)ch; buf[23] = 0;
    buf[24] = (unsigned char)rate; buf[25] = (unsigned char)(rate >> 8); buf[26] = (unsigned char)(rate >> 16); buf[27] = 0;
    buf[28] = (unsigned char)avg; buf[29] = (unsigned char)(avg >> 8); buf[30] = (unsigned char)(avg >> 16); buf[31] = 0;
    buf[32] = (unsigned char)(ch * 2); buf[33] = 0;
    buf[34] = 16; buf[35] = 0;
    memcpy(buf + 36, "data", 4);
    buf[40] = (unsigned char)dataSize; buf[41] = (unsigned char)(dataSize >> 8); buf[42] = (unsigned char)(dataSize >> 16); buf[43] = (unsigned char)(dataSize >> 24);
    /* crude fixed-point sine via table-free triangle-ish oscillation is not a
     * sine; use slow but exact loop with doubles, this is a test tool */
    for (int i = 0; i < frames; i++)
    {
        double t = (double)i / rate;
        double v = 0.4 * sin(6.283185307179586 * 440.0 * t);
        short s = (short)(v * 32767.0);
        for (int c = 0; c < ch; c++)
        {
            size_t off = 44 + ((size_t)i * ch + c) * 2;
            buf[off] = (unsigned char)(s & 0xFF);
            buf[off + 1] = (unsigned char)((s >> 8) & 0xFF);
        }
    }
    int r = write_file(path, buf, 44 + dataSize);
    free(buf);
    return r;
}

static int find_entry(smzip_reader *r, const wchar_t *name)
{
    wchar_t buf[512];
    int n = smzip_entry_count(r);
    for (int i = 0; i < n; i++)
    {
        if (smzip_entry_name(r, i, buf, 512) >= 0 && wcscmp(buf, name) == 0)
            return i;
    }
    return -1;
}

static void selftest(const wchar_t *tmpdir)
{
    printf("[zip round-trip]\n");
    size_t textLen = 200000, randLen = 100000;
    unsigned char *text = gen_text(textLen);
    unsigned char *rnd = gen_random(randLen, 42);
    const wchar_t *zpath = path_join(tmpdir, L"selftest.zip");

    smzip_writer *w = NULL;
    check(smzipw_create(zpath, &w) == SMN_OK, "writer create");
    check(smzipw_add_data(w, L"text.txt", text, (long long)textLen) == SMN_OK, "add compressible data");
    check(smzipw_add_data(w, L"random.bin", rnd, (long long)randLen) == SMN_OK, "add incompressible data");
    check(smzipw_add_data(w, L"empty.txt", (const unsigned char *)"", 0) == SMN_OK, "add empty entry");
    check(smzipw_add_data(w, L"日本語名前.txt", (const unsigned char *)"utf8", 4) == SMN_OK, "add UTF-8 named entry");
    check(smzipw_close(w, 0) == SMN_OK, "writer close");

    check(smzip_check(zpath) == SMN_OK, "check valid zip");

    smzip_reader *r = NULL;
    check(smzip_open(zpath, &r) == SMN_OK, "reader open");
    if (r)
    {
        check(smzip_entry_count(r) == 4, "entry count == 4");
        int it = find_entry(r, L"text.txt");
        int ir = find_entry(r, L"random.bin");
        int ie = find_entry(r, L"empty.txt");
        int iu = find_entry(r, L"日本語名前.txt");
        check(it >= 0 && ir >= 0 && ie >= 0 && iu >= 0, "all entries found by name");
        check(smzip_entry_size(r, it) == (long long)textLen, "text entry size");
        check(smzip_entry_size(r, ie) == 0, "empty entry size");

        unsigned char *back = (unsigned char *)malloc(textLen);
        check(smzip_extract_to_memory(r, it, back, (long long)textLen) == SMN_OK, "extract text to memory");
        check(memcmp(back, text, textLen) == 0, "text content matches");
        free(back);

        back = (unsigned char *)malloc(randLen);
        check(smzip_extract_to_memory(r, ir, back, (long long)randLen) == SMN_OK, "extract random to memory");
        check(memcmp(back, rnd, randLen) == 0, "random content matches");
        free(back);

        const wchar_t *xpath = path_join(tmpdir, L"extracted_text.txt");
        check(smzip_extract_to_file(r, it, xpath) == SMN_OK, "extract to file");
        size_t xlen = 0;
        unsigned char *xdata = read_file(xpath, &xlen);
        check(xdata && xlen == textLen && memcmp(xdata, text, textLen) == 0, "extracted file content matches");
        free(xdata);

        smzip_close(r);
    }

    printf("[zip corruption detection]\n");
    size_t zlen = 0;
    unsigned char *zdata = read_file(zpath, &zlen);
    if (zdata && zlen > 100)
    {
        zdata[60] ^= 0xFF; /* flip a byte inside first entry's data */
        const wchar_t *cpath = path_join(tmpdir, L"corrupt.zip");
        write_file(cpath, zdata, zlen);
        smzip_reader *cr = NULL;
        if (smzip_open(cpath, &cr) == SMN_OK)
        {
            unsigned char *tmp = (unsigned char *)malloc(textLen);
            int e = smzip_extract_to_memory(cr, find_entry(cr, L"text.txt"), tmp, (long long)textLen);
            check(e == SMN_E_CRC || e == SMN_E_FORMAT, "corrupted entry rejected");
            free(tmp);
            smzip_close(cr);
        }
        else
            check(1, "corrupted zip rejected at open");
    }
    free(zdata);

    const wchar_t *njpath = path_join(tmpdir, L"notazip.bin");
    unsigned char *nj = gen_random(5000, 7);
    write_file(njpath, nj, 5000);
    free(nj);
    check(smzip_check(njpath) != SMN_OK, "non-zip rejected by check");

    printf("[audio transcode]\n");
    const wchar_t *wav1 = path_join(tmpdir, L"tone.wav");
    const wchar_t *wav2 = path_join(tmpdir, L"tone_out.wav");
    check(gen_wav(wav1) == 0, "generate test wav");
    long long dur = smaudio_duration_100ns(wav1);
    printf("      duration = %lld (100ns units)\n", dur);
    check(dur > 9000000 && dur < 11000000, "duration ~1 second");
    check(smaudio_transcode_wav(wav1, wav2) == SMN_OK, "transcode wav -> wav");
    size_t olen = 0;
    unsigned char *odata = read_file(wav2, &olen);
    check(odata != NULL && olen > 44, "output exists");
    if (odata && olen > 44)
    {
        check(memcmp(odata, "RIFF", 4) == 0 && memcmp(odata + 8, "WAVE", 4) == 0, "output is RIFF/WAVE");
        unsigned int fmtTag = odata[20] | (odata[21] << 8);
        unsigned int bits = odata[34] | (odata[35] << 8);
        unsigned int chans = odata[22] | (odata[23] << 8);
        printf("      fmt=%u bits=%u channels=%u size=%u\n", fmtTag, bits, chans, (unsigned int)olen);
        check(fmtTag == 1, "output is PCM");
        check(bits == 16, "output is 16-bit");
    }
    free(odata);

    long long badDur = smaudio_duration_100ns(njpath);
    check(badDur < 0, "non-media duration fails cleanly");

    printf("[tui spec roundtrip]\n");
    {
        int tres = smtui_selftest();
        printf("      smtui_selftest = %d\n", tres);
        check(tres == 0, "tui spec parse/build/collect");
    }

    free(text);
    free(rnd);
    printf("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
}

static const wchar_t *basename_of(const wchar_t *p)
{
    const wchar_t *b = p;
    for (const wchar_t *q = p; *q; q++)
        if (*q == L'\\' || *q == L'/')
            b = q + 1;
    return b;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc >= 3 && wcscmp(argv[1], L"selftest") == 0)
    {
        selftest(argv[2]);
        return failures ? 1 : 0;
    }
    if (argc >= 4 && wcscmp(argv[1], L"create") == 0)
    {
        smzip_writer *w = NULL;
        if (smzipw_create(argv[2], &w) != SMN_OK)
            return 1;
        for (int i = 3; i < argc; i++)
            if (smzipw_add_file(w, basename_of(argv[i]), argv[i]) != SMN_OK)
            {
                smzipw_close(w, 1);
                return 1;
            }
        return smzipw_close(w, 0) == SMN_OK ? 0 : 1;
    }
    if (argc >= 4 && wcscmp(argv[1], L"extract") == 0)
    {
        smzip_reader *r = NULL;
        if (smzip_open(argv[2], &r) != SMN_OK)
            return 1;
        int n = smzip_entry_count(r);
        for (int i = 0; i < n; i++)
        {
            wchar_t name[512];
            if (smzip_entry_name(r, i, name, 512) < 0)
                continue;
            if (wcschr(name, L'/') || wcschr(name, L'\\'))
                continue; /* flat names only in this test tool */
            if (smzip_extract_to_file(r, i, path_join(argv[3], name)) != SMN_OK)
            {
                smzip_close(r);
                return 1;
            }
        }
        smzip_close(r);
        return 0;
    }
    if (argc >= 3 && wcscmp(argv[1], L"list") == 0)
    {
        smzip_reader *r = NULL;
        int e = smzip_open(argv[2], &r);
        if (e != SMN_OK)
        {
            printf("open error %d\n", e);
            return 1;
        }
        int n = smzip_entry_count(r);
        for (int i = 0; i < n; i++)
        {
            wchar_t name[512];
            smzip_entry_name(r, i, name, 512);
            printf("%8lld  %ls\n", smzip_entry_size(r, i), name);
        }
        smzip_close(r);
        return 0;
    }
    printf("usage: test selftest <tmpdir> | create <zip> <files...> | extract <zip> <outdir> | list <zip>\n");
    return 2;
}
