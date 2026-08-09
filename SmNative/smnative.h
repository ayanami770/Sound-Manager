/*
 * SmNative - self-contained zip archive and audio transcoding routines
 * for SoundManager, replacing the third-party Ionic.Zip and NAudio libraries.
 * Written in C99 against the Windows API only, no external dependencies.
 * By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
 */
#ifndef SMNATIVE_H
#define SMNATIVE_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SMNATIVE_BUILD
#define SMN_API __declspec(dllexport)
#else
#define SMN_API __declspec(dllimport)
#endif

/* Error codes returned by all functions (negative = failure) */
#define SMN_OK             0
#define SMN_E_IO          -1  /* file open/read/write failed */
#define SMN_E_FORMAT      -2  /* not a zip file or corrupt structure */
#define SMN_E_UNSUPPORTED -3  /* unsupported feature (zip64, encryption, compression method) */
#define SMN_E_CRC         -4  /* CRC-32 mismatch on extraction */
#define SMN_E_ARG         -5  /* invalid argument */
#define SMN_E_MEM         -6  /* memory allocation failure */
#define SMN_E_NOTFOUND    -7  /* entry index out of range */
#define SMN_E_AUDIO_INIT  -10 /* Media Foundation unavailable */
#define SMN_E_AUDIO_OPEN  -11 /* cannot open or decode source media file */
#define SMN_E_AUDIO_WRITE -12 /* cannot write output WAV file */

/* --- Zip reading --- */

typedef struct smzip_reader smzip_reader;

/* Open a zip file and parse its central directory. */
SMN_API int smzip_open(const wchar_t *path, smzip_reader **out);
SMN_API void smzip_close(smzip_reader *r);

/* Validate that a file is a well-formed zip archive (0 = valid). */
SMN_API int smzip_check(const wchar_t *path);

SMN_API int smzip_entry_count(smzip_reader *r);

/* Get entry name. Returns required length in wchars (excluding NUL),
 * or negative error. Fills buf up to bufchars (always NUL-terminated). */
SMN_API int smzip_entry_name(smzip_reader *r, int index, wchar_t *buf, int bufchars);

/* Uncompressed size of an entry, or negative error. */
SMN_API long long smzip_entry_size(smzip_reader *r, int index);

/* Extract one entry. CRC-32 is verified; output file is overwritten. */
SMN_API int smzip_extract_to_file(smzip_reader *r, int index, const wchar_t *outPath);

/* Extract one entry into caller buffer of at least smzip_entry_size() bytes. */
SMN_API int smzip_extract_to_memory(smzip_reader *r, int index, unsigned char *buf, long long bufsize);

/* --- Zip writing --- */

typedef struct smzip_writer smzip_writer;

/* Create a new zip file (truncates existing). */
SMN_API int smzipw_create(const wchar_t *path, smzip_writer **out);

/* Append one entry from a file on disk (deflate compressed). */
SMN_API int smzipw_add_file(smzip_writer *w, const wchar_t *entryName, const wchar_t *srcPath);

/* Append one entry from memory (deflate compressed). */
SMN_API int smzipw_add_data(smzip_writer *w, const wchar_t *entryName, const unsigned char *data, long long size);

/* Write central directory and close. If discard is nonzero, the output
 * file is deleted instead. Frees the writer in all cases. */
SMN_API int smzipw_close(smzip_writer *w, int discard);

/* --- Audio (Media Foundation, Windows 7+) --- */

/* Duration of a media file in 100-nanosecond units (same as .NET TimeSpan
 * ticks), or negative error. */
SMN_API long long smaudio_duration_100ns(const wchar_t *path);

/* Decode any Media Foundation supported format (MP3, AAC, WMA, WAV, ...)
 * and write a PCM WAV file. */
SMN_API int smaudio_transcode_wav(const wchar_t *srcPath, const wchar_t *dstPath);

/* --- Internal shared helpers (not exported) --- */
#ifdef SMNATIVE_BUILD
unsigned int smn_crc32(unsigned int crc, const unsigned char *buf, size_t len);
int smn_inflate(const unsigned char *src, size_t srclen, unsigned char *dst, size_t dstlen);
unsigned char *smn_deflate(const unsigned char *src, size_t srclen, size_t *outlen);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SMNATIVE_H */
