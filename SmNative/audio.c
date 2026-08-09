/*
 * Audio transcoding via Windows Media Foundation (Windows 7+), replacing
 * NAudio's MediaFoundationReader + WaveFormatConversionStream + WaveFileWriter.
 * mfplat.dll / mfreadwrite.dll are loaded dynamically so that the DLL still
 * loads on systems without Media Foundation (zip functions keep working).
 * By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
 */
#ifndef SMNATIVE_BUILD
#define SMNATIVE_BUILD
#endif
#define COBJMACROS
#define INITGUID
#include "smnative.h"

#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>

typedef HRESULT(WINAPI *fnMFStartup)(ULONG, DWORD);
typedef HRESULT(WINAPI *fnMFCreateSourceReaderFromURL)(LPCWSTR, IMFAttributes *, IMFSourceReader **);
typedef HRESULT(WINAPI *fnMFCreateMediaType)(IMFMediaType **);
typedef HRESULT(WINAPI *fnMFCreateWaveFormatExFromMFMediaType)(IMFMediaType *, WAVEFORMATEX **, UINT32 *, UINT32);

static fnMFStartup pMFStartup;
static fnMFCreateSourceReaderFromURL pMFCreateSourceReaderFromURL;
static fnMFCreateMediaType pMFCreateMediaType;
static fnMFCreateWaveFormatExFromMFMediaType pMFCreateWaveFormatExFromMFMediaType;
static int mf_state = 0; /* 0 = not tried, 1 = ready, -1 = unavailable */

static int ensure_mf(void)
{
    if (mf_state != 0)
        return mf_state;

    HMODULE mfplat = LoadLibraryW(L"mfplat.dll");
    HMODULE mfread = LoadLibraryW(L"mfreadwrite.dll");
    if (mfplat && mfread)
    {
        pMFStartup = (fnMFStartup)(void *)GetProcAddress(mfplat, "MFStartup");
        pMFCreateSourceReaderFromURL = (fnMFCreateSourceReaderFromURL)(void *)GetProcAddress(mfread, "MFCreateSourceReaderFromURL");
        pMFCreateMediaType = (fnMFCreateMediaType)(void *)GetProcAddress(mfplat, "MFCreateMediaType");
        pMFCreateWaveFormatExFromMFMediaType = (fnMFCreateWaveFormatExFromMFMediaType)(void *)GetProcAddress(mfplat, "MFCreateWaveFormatExFromMFMediaType");
    }

    if (pMFStartup && pMFCreateSourceReaderFromURL && pMFCreateMediaType && pMFCreateWaveFormatExFromMFMediaType
        && SUCCEEDED(pMFStartup(MF_VERSION, MFSTARTUP_FULL)))
    {
        mf_state = 1;
    }
    else
    {
        mf_state = -1;
    }
    return mf_state;
}

static void init_com(void)
{
    /* S_OK, S_FALSE and RPC_E_CHANGED_MODE (already STA from WinForms) are
     * all fine: COM is usable on this thread in every case. */
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
}

static int open_reader(const wchar_t *path, IMFSourceReader **reader)
{
    if (ensure_mf() != 1)
        return SMN_E_AUDIO_INIT;
    init_com();
    if (FAILED(pMFCreateSourceReaderFromURL(path, NULL, reader)))
        return SMN_E_AUDIO_OPEN;
    return SMN_OK;
}

long long smaudio_duration_100ns(const wchar_t *path)
{
    if (!path)
        return SMN_E_ARG;

    IMFSourceReader *reader = NULL;
    int err = open_reader(path, &reader);
    if (err != SMN_OK)
        return err;

    PROPVARIANT pv;
    PropVariantInit(&pv);
    long long result = SMN_E_AUDIO_OPEN;
    if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(reader, (DWORD)MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION, &pv))
        && pv.vt == VT_UI8)
    {
        result = (long long)pv.uhVal.QuadPart;
    }
    PropVariantClear(&pv);
    IMFSourceReader_Release(reader);
    return result;
}

/* Configure the source reader to output PCM. Tries 16-bit first, then lets
 * Media Foundation choose the bit depth. */
static int set_pcm_output(IMFSourceReader *reader)
{
    for (int attempt = 0; attempt < 2; attempt++)
    {
        IMFMediaType *mt = NULL;
        if (FAILED(pMFCreateMediaType(&mt)))
            return SMN_E_AUDIO_OPEN;
        HRESULT hr = IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        if (SUCCEEDED(hr))
            hr = IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        if (SUCCEEDED(hr) && attempt == 0)
            hr = IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr))
            hr = IMFSourceReader_SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mt);
        IMFMediaType_Release(mt);
        if (SUCCEEDED(hr))
            return SMN_OK;
    }
    return SMN_E_AUDIO_OPEN;
}

static int wav_write_header(FILE *f, const WAVEFORMATEX *wfx, unsigned int dataSize)
{
    unsigned char h[44];
    unsigned int riffSize = 36 + dataSize;
    unsigned int avg = wfx->nAvgBytesPerSec;
    memcpy(h, "RIFF", 4);
    h[4] = (unsigned char)(riffSize); h[5] = (unsigned char)(riffSize >> 8);
    h[6] = (unsigned char)(riffSize >> 16); h[7] = (unsigned char)(riffSize >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0; /* fmt chunk size */
    h[20] = (unsigned char)(wfx->wFormatTag); h[21] = (unsigned char)(wfx->wFormatTag >> 8);
    h[22] = (unsigned char)(wfx->nChannels); h[23] = (unsigned char)(wfx->nChannels >> 8);
    h[24] = (unsigned char)(wfx->nSamplesPerSec); h[25] = (unsigned char)(wfx->nSamplesPerSec >> 8);
    h[26] = (unsigned char)(wfx->nSamplesPerSec >> 16); h[27] = (unsigned char)(wfx->nSamplesPerSec >> 24);
    h[28] = (unsigned char)(avg); h[29] = (unsigned char)(avg >> 8);
    h[30] = (unsigned char)(avg >> 16); h[31] = (unsigned char)(avg >> 24);
    h[32] = (unsigned char)(wfx->nBlockAlign); h[33] = (unsigned char)(wfx->nBlockAlign >> 8);
    h[34] = (unsigned char)(wfx->wBitsPerSample); h[35] = (unsigned char)(wfx->wBitsPerSample >> 8);
    memcpy(h + 36, "data", 4);
    h[40] = (unsigned char)(dataSize); h[41] = (unsigned char)(dataSize >> 8);
    h[42] = (unsigned char)(dataSize >> 16); h[43] = (unsigned char)(dataSize >> 24);
    return fwrite(h, 1, 44, f) == 44 ? SMN_OK : SMN_E_AUDIO_WRITE;
}

int smaudio_transcode_wav(const wchar_t *srcPath, const wchar_t *dstPath)
{
    if (!srcPath || !dstPath)
        return SMN_E_ARG;

    IMFSourceReader *reader = NULL;
    IMFMediaType *outType = NULL;
    WAVEFORMATEX *wfx = NULL;
    FILE *out = NULL;
    int err = open_reader(srcPath, &reader);
    if (err != SMN_OK)
        return err;

    err = set_pcm_output(reader);
    if (err != SMN_OK)
        goto done;

    err = SMN_E_AUDIO_OPEN;
    if (FAILED(IMFSourceReader_GetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &outType)))
        goto done;
    UINT32 wfxSize = 0;
    if (FAILED(pMFCreateWaveFormatExFromMFMediaType(outType, &wfx, &wfxSize, 0)))
        goto done;

    out = _wfopen(dstPath, L"wb");
    if (!out)
    {
        err = SMN_E_AUDIO_WRITE;
        goto done;
    }
    err = wav_write_header(out, wfx, 0);
    if (err != SMN_OK)
        goto done;

    unsigned long long dataSize = 0;
    for (;;)
    {
        DWORD flags = 0;
        IMFSample *sample = NULL;
        if (FAILED(IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, NULL, &sample)))
        {
            err = SMN_E_AUDIO_OPEN;
            goto done;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            if (sample)
                IMFSample_Release(sample);
            break;
        }
        if (!sample)
            continue;

        IMFMediaBuffer *buffer = NULL;
        if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)))
        {
            BYTE *data = NULL;
            DWORD len = 0;
            if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, &len)))
            {
                size_t written = fwrite(data, 1, len, out);
                IMFMediaBuffer_Unlock(buffer);
                if (written != len)
                {
                    IMFMediaBuffer_Release(buffer);
                    IMFSample_Release(sample);
                    err = SMN_E_AUDIO_WRITE;
                    goto done;
                }
                dataSize += len;
            }
            IMFMediaBuffer_Release(buffer);
        }
        IMFSample_Release(sample);
    }

    /* rewrite header with final sizes */
    if (dataSize > 0xFFFFFFF0ull - 44)
    {
        err = SMN_E_AUDIO_WRITE;
        goto done;
    }
    if (fseek(out, 0, SEEK_SET) != 0)
    {
        err = SMN_E_AUDIO_WRITE;
        goto done;
    }
    err = wav_write_header(out, wfx, (unsigned int)dataSize);

done:
    if (out)
    {
        int cerr = fclose(out);
        if (err == SMN_OK && cerr != 0)
            err = SMN_E_AUDIO_WRITE;
        if (err != SMN_OK)
            _wremove(dstPath);
    }
    if (wfx)
        CoTaskMemFree(wfx);
    if (outType)
        IMFMediaType_Release(outType);
    if (reader)
        IMFSourceReader_Release(reader);
    return err;
}
