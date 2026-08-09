using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace SoundManager
{
    /// <summary>
    /// Managed bindings for SmNative.dll: self-contained zip archive and audio
    /// transcoding routines replacing the Ionic.Zip and NAudio libraries.
    /// By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
    /// </summary>
    static class SmNative
    {
        private const string DllName = "SmNative.dll";

        public const int OK = 0;
        public const int E_IO = -1;
        public const int E_FORMAT = -2;
        public const int E_UNSUPPORTED = -3;
        public const int E_CRC = -4;
        public const int E_ARG = -5;
        public const int E_MEM = -6;
        public const int E_NOTFOUND = -7;
        public const int E_AUDIO_INIT = -10;
        public const int E_AUDIO_OPEN = -11;
        public const int E_AUDIO_WRITE = -12;

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int smzip_open(string path, out IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void smzip_close(IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int smzip_check(string path);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int smzip_entry_count(IntPtr reader);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int smzip_entry_name(IntPtr reader, int index, StringBuilder buf, int bufchars);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern long smzip_entry_size(IntPtr reader, int index);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int smzip_extract_to_file(IntPtr reader, int index, string outPath);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int smzip_extract_to_memory(IntPtr reader, int index, byte[] buf, long bufsize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int smzipw_create(string path, out IntPtr writer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int smzipw_add_file(IntPtr writer, string entryName, string srcPath);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int smzipw_add_data(IntPtr writer, string entryName, byte[] data, long size);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int smzipw_close(IntPtr writer, int discard);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern long smaudio_duration_100ns(string path);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int smaudio_transcode_wav(string srcPath, string dstPath);

        /// <summary>
        /// Convert a SmNative error code to an appropriate exception
        /// </summary>
        /// <param name="operation">Description of the failed operation, e.g. file path</param>
        /// <param name="code">Negative SmNative error code</param>
        public static Exception Error(string operation, int code)
        {
            switch (code)
            {
                case E_IO:
                case E_AUDIO_WRITE:
                    return new IOException(String.Format("{0}: I/O error (SmNative {1})", operation, code));
                case E_FORMAT:
                case E_CRC:
                    return new InvalidDataException(String.Format("{0}: invalid or corrupt data (SmNative {1})", operation, code));
                case E_UNSUPPORTED:
                    return new NotSupportedException(String.Format("{0}: unsupported archive feature (SmNative {1})", operation, code));
                case E_NOTFOUND:
                    return new FileNotFoundException(String.Format("{0}: entry not found (SmNative {1})", operation, code));
                case E_AUDIO_INIT:
                case E_AUDIO_OPEN:
                    return new InvalidOperationException(String.Format("{0}: cannot decode media file (SmNative {1})", operation, code));
                default:
                    return new InvalidOperationException(String.Format("{0}: SmNative error {1}", operation, code));
            }
        }
    }

    /// <summary>
    /// Read-only view of a zip archive backed by SmNative.dll
    /// </summary>
    class SmZipReader : IDisposable
    {
        private IntPtr handle;
        private readonly string[] entries;

        /// <summary>
        /// Open a zip file and read its table of contents
        /// </summary>
        /// <param name="path">Path to the zip file</param>
        public SmZipReader(string path)
        {
            int err = SmNative.smzip_open(path, out handle);
            if (err != SmNative.OK)
                throw SmNative.Error(path, err);

            entries = new string[SmNative.smzip_entry_count(handle)];
            for (int i = 0; i < entries.Length; i++)
            {
                StringBuilder buf = new StringBuilder(512);
                int need = SmNative.smzip_entry_name(handle, i, buf, buf.Capacity);
                if (need >= buf.Capacity)
                {
                    buf = new StringBuilder(need + 1);
                    SmNative.smzip_entry_name(handle, i, buf, buf.Capacity);
                }
                entries[i] = buf.ToString();
            }
        }

        /// <summary>
        /// Entry names in archive order
        /// </summary>
        public string[] Entries
        {
            get { return entries; }
        }

        /// <summary>
        /// Find an entry by name
        /// </summary>
        /// <param name="fileName">Entry name to look for</param>
        /// <param name="ignoreCase">TRUE for case-insensitive comparison</param>
        /// <returns>Entry index or -1 if not found</returns>
        public int FindEntry(string fileName, bool ignoreCase)
        {
            StringComparison cmp = ignoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
            for (int i = 0; i < entries.Length; i++)
                if (String.Equals(entries[i], fileName, cmp))
                    return i;
            return -1;
        }

        /// <summary>
        /// Check whether the archive contains the specified entry (case-insensitive)
        /// </summary>
        public bool ContainsEntry(string fileName)
        {
            return FindEntry(fileName, true) >= 0;
        }

        /// <summary>
        /// Extract an entry to the specified file, overwriting silently.
        /// CRC-32 is verified during extraction.
        /// </summary>
        public void ExtractToFile(int index, string outputPath)
        {
            int err = SmNative.smzip_extract_to_file(handle, index, outputPath);
            if (err != SmNative.OK)
                throw SmNative.Error(entries[index], err);
        }

        /// <summary>
        /// Extract an entry into a byte array. CRC-32 is verified during extraction.
        /// </summary>
        public byte[] ExtractToBytes(int index)
        {
            long size = SmNative.smzip_entry_size(handle, index);
            if (size < 0)
                throw SmNative.Error("entry " + index, (int)size);
            byte[] data = new byte[size];
            int err = SmNative.smzip_extract_to_memory(handle, index, data, size);
            if (err != SmNative.OK)
                throw SmNative.Error(entries[index], err);
            return data;
        }

        /// <summary>
        /// Check whether the specified file is a well-formed zip archive
        /// </summary>
        public static bool Check(string path)
        {
            return SmNative.smzip_check(path) == SmNative.OK;
        }

        public void Dispose()
        {
            if (handle != IntPtr.Zero)
            {
                SmNative.smzip_close(handle);
                handle = IntPtr.Zero;
            }
        }
    }

    /// <summary>
    /// Zip archive creator backed by SmNative.dll. Entries are buffered in
    /// memory and written out when calling Save().
    /// </summary>
    class SmZipWriter : IDisposable
    {
        private struct PendingEntry
        {
            public string Name;
            public string SourceFile; /* either a source file path... */
            public byte[] Data;       /* ...or raw data */
        }

        private readonly List<PendingEntry> pending = new List<PendingEntry>();

        /// <summary>
        /// Add a file to the archive root, named after its file name
        /// </summary>
        public void AddFile(string filePath)
        {
            PendingEntry entry = new PendingEntry();
            entry.Name = Path.GetFileName(filePath);
            entry.SourceFile = filePath;
            pending.Add(entry);
        }

        /// <summary>
        /// Add an entry from raw data
        /// </summary>
        public void AddEntry(string entryName, byte[] data)
        {
            PendingEntry entry = new PendingEntry();
            entry.Name = entryName;
            entry.Data = data;
            pending.Add(entry);
        }

        /// <summary>
        /// Write all pending entries to the specified zip file (overwritten if existing)
        /// </summary>
        public void Save(string outputPath)
        {
            IntPtr writer;
            int err = SmNative.smzipw_create(outputPath, out writer);
            if (err != SmNative.OK)
                throw SmNative.Error(outputPath, err);

            foreach (PendingEntry entry in pending)
            {
                if (entry.SourceFile != null)
                    err = SmNative.smzipw_add_file(writer, entry.Name, entry.SourceFile);
                else
                    err = SmNative.smzipw_add_data(writer, entry.Name, entry.Data, entry.Data.LongLength);
                if (err != SmNative.OK)
                {
                    SmNative.smzipw_close(writer, 1);
                    throw SmNative.Error(entry.Name, err);
                }
            }

            err = SmNative.smzipw_close(writer, 0);
            if (err != SmNative.OK)
                throw SmNative.Error(outputPath, err);
        }

        public void Dispose()
        {
            pending.Clear();
        }
    }

    /// <summary>
    /// Audio file inspection and conversion through Windows Media Foundation
    /// (Windows 7 and greater), backed by SmNative.dll
    /// </summary>
    static class SmAudio
    {
        /// <summary>
        /// Get the duration of a media file
        /// </summary>
        /// <exception cref="InvalidOperationException">File cannot be opened or decoded</exception>
        public static TimeSpan GetDuration(string mediaFile)
        {
            long ticks = SmNative.smaudio_duration_100ns(mediaFile);
            if (ticks < 0)
                throw SmNative.Error(mediaFile, (int)ticks);
            return TimeSpan.FromTicks(ticks); /* 100ns units == TimeSpan ticks */
        }

        /// <summary>
        /// Decode any Media Foundation supported audio file (MP3, AAC, WMA, ...)
        /// and write it as a PCM WAV file that Windows can play natively.
        /// </summary>
        public static void TranscodeToWav(string inputMediaFile, string outputWavFile)
        {
            int err = SmNative.smaudio_transcode_wav(inputMediaFile, outputWavFile);
            if (err != SmNative.OK)
                throw SmNative.Error(inputMediaFile, err);
        }
    }
}
