using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using SharpTools;

namespace SoundManager
{
    /// <summary>
    /// Command line interface of SoundManager: everything the former WinForms GUI
    /// did that is not configuration editing (which lives in the TUI, see SmTui).
    /// Also absorbs the former DownloadSchemes utility as the "download" command.
    /// By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
    /// </summary>
    static class Cli
    {
        private static readonly string SchemesListTempFile = Path.Combine(Path.GetTempPath(), RuntimeConfig.SchemesRepositoryName.ToLowerInvariant() + ".txt");

        private static readonly Dictionary<string, string> SchemesPerNtVersion = new Dictionary<string, string>
        {
            { "6.0", "Windows-Vista-7.ths" },
            { "6.1", "Windows-Vista-7.ths" },
            { "6.2", "Windows-8.ths" },
            { "6.3", "Windows-8.ths" },
            { "10.0", "Windows-10.ths" },
            { "10.0_11", "Windows-11.ths" },
        };

        /// <summary>
        /// Print an error message to stderr
        /// </summary>
        public static void Error(string message)
        {
            Console.Error.WriteLine(message);
        }

        /// <summary>
        /// Check whether stdin is redirected (.NET 4.0 compatible: Console.KeyAvailable
        /// throws InvalidOperationException when stdin is not a console)
        /// </summary>
        public static bool StdinRedirected()
        {
            try { if (Console.KeyAvailable) { } return false; }
            catch (InvalidOperationException) { return true; }
        }

        /// <summary>
        /// Interactive yes/no prompt. Defaults to NO when the console is redirected.
        /// </summary>
        public static bool Confirm(string prompt)
        {
            if (StdinRedirected())
                return false;
            Console.Write(prompt + " [y/N] ");
            string answer = Console.ReadLine();
            return answer != null && (answer.Trim().ToLowerInvariant() == "y" || answer.Trim().ToLowerInvariant() == "yes");
        }

        /// <summary>
        /// Dispatch a CLI subcommand
        /// </summary>
        /// <returns>Process exit code</returns>
        public static int Run(string[] args)
        {
            string command = args[0].ToLowerInvariant();
            string[] rest = args.Skip(1).ToArray();
            try
            {
                switch (command)
                {
                    case "events": return CmdEvents();
                    case "get": return CmdGet(rest);
                    case "set": return CmdSet(rest);
                    case "play": return CmdPlay(rest);
                    case "reset": return CmdReset(rest);
                    case "import": return CmdImport(rest);
                    case "export": return CmdExport(rest);
                    case "meta": return CmdMeta(rest);
                    case "schemes": return CmdSchemes();
                    case "apply": return CmdApply();
                    case "download": return CmdDownload(rest);
                    case "setup":
                        Program.Setup(forceResetSounds: false, systemIntegration: true, offerImportCurrentScheme: false);
                        return 0;
                    case "uninstall":
                        if (!Confirm(Translations.Get("uninstall_confirm_text")))
                            return 1;
                        Program.Uninstall();
                        return 0;
                    case "reinstall":
                        if (!Confirm(Translations.Get("reinstall_confirm_text")))
                            return 1;
                        Program.Uninstall();
                        Program.Setup(forceResetSounds: true, systemIntegration: true, offerImportCurrentScheme: false);
                        return 0;
                    case "version":
                        Console.WriteLine(RuntimeConfig.AppDisplayName + " " + RuntimeConfig.Version);
                        return 0;
                    case "help": case "--help": case "-h": case "/?":
                        PrintUsage();
                        return 0;
                    default:
                        Error("Unknown command: " + command);
                        PrintUsage();
                        return 1;
                }
            }
            catch (Exception e)
            {
                Error(e.Message);
                return 1;
            }
        }

        /// <summary>
        /// Print CLI usage. Command names are not translated (like most CLI tools);
        /// the interactive TUI carries the translated experience.
        /// </summary>
        public static void PrintUsage()
        {
            string exe = RuntimeConfig.AppInternalName;
            Console.WriteLine(RuntimeConfig.AppDisplayName + " " + RuntimeConfig.Version);
            Console.WriteLine();
            Console.WriteLine("usage: " + exe + " [command]");
            Console.WriteLine();
            Console.WriteLine("  (no command)          open the configuration TUI");
            Console.WriteLine("  events                list sound events and assigned files");
            Console.WriteLine("  get <event>           show the file assigned to a sound event");
            Console.WriteLine("  set <event> <file>    assign a sound file (auto-converted to WAV)");
            Console.WriteLine("  set <event> none      remove the assigned sound");
            Console.WriteLine("  play <event>          play the current sound of an event");
            Console.WriteLine("  reset [scheme]        reset all sounds to a system scheme (default: system default)");
            Console.WriteLine("  import <file>         import a ." + SoundArchive.FileExtension + " / ." + SoundArchiveProprietary.FileExtension + " sound scheme archive");
            Console.WriteLine("  export <file>         export to ." + SoundArchive.FileExtension + " (or ." + SoundArchiveThemepack.FileExtension + ")");
            Console.WriteLine("  meta [name=X] [author=X] [about=X] [image=path|none]");
            Console.WriteLine("                        show or edit scheme metadata");
            Console.WriteLine("  schemes               list system sound schemes");
            Console.WriteLine("  download [all|name..] download sound schemes from the schemes repository");
            Console.WriteLine("  apply                 re-apply the SoundManager scheme to the system");
            Console.WriteLine("  setup                 create scheme, file associations and system integration");
            Console.WriteLine("  reinstall             reset everything and set up again");
            Console.WriteLine("  uninstall             remove the scheme and all system integration");
            Console.WriteLine("  version               show program version");
        }

        private static SoundEvent FindEvent(string name)
        {
            SoundEvent ev = SoundEvent.GetAll().FirstOrDefault(e =>
                String.Equals(e.InternalName, name, StringComparison.OrdinalIgnoreCase)
                || String.Equals(e.DisplayName, name, StringComparison.OrdinalIgnoreCase));
            if (ev == null)
                throw new ArgumentException("Unknown sound event: " + name + " (see '" + RuntimeConfig.AppInternalName + " events')");
            return ev;
        }

        private static int CmdEvents()
        {
            foreach (SoundEvent ev in SoundEvent.GetAll())
            {
                Console.WriteLine("{0,-18} {1} {2}",
                    ev.InternalName,
                    ev.Disabled ? "[disabled]" : (File.Exists(ev.FilePath) ? "[assigned]" : "[   -    ]"),
                    File.Exists(ev.FilePath) ? ev.FilePath : "");
            }
            return 0;
        }

        private static int CmdGet(string[] args)
        {
            if (args.Length < 1) { Error("usage: get <event>"); return 1; }
            SoundEvent ev = FindEvent(args[0]);
            Console.WriteLine(File.Exists(ev.FilePath) ? ev.FilePath : "");
            return 0;
        }

        private static int CmdSet(string[] args)
        {
            if (args.Length < 2) { Error("usage: set <event> <file|none>"); return 1; }
            SoundEvent ev = FindEvent(args[0]);
            if (args[1].ToLowerInvariant() == "none")
            {
                SoundScheme.Remove(ev);
            }
            else
            {
                if (!File.Exists(args[1])) { Error("File not found: " + args[1]); return 1; }
                SoundScheme.Update(ev, Path.GetFullPath(args[1]));
            }
            SoundScheme.Apply(SoundScheme.GetSchemeSoundManager(), Settings.MissingSoundUseDefault);
            return 0;
        }

        private static int CmdPlay(string[] args)
        {
            if (args.Length < 1) { Error("usage: play <event>"); return 1; }
            SoundEvent ev = FindEvent(args[0]);
            if (!File.Exists(ev.FilePath)) { Error("No sound assigned."); return 1; }
            System.Media.SoundPlayer player = new System.Media.SoundPlayer(ev.FilePath);
            player.PlaySync();
            return 0;
        }

        private static int CmdReset(string[] args)
        {
            SoundScheme source = null;
            if (args.Length > 0)
            {
                source = SoundScheme.GetSchemeList().FirstOrDefault(s =>
                    String.Equals(s.ToString(), String.Join(" ", args), StringComparison.OrdinalIgnoreCase));
                if (source == null) { Error("Unknown scheme. See '" + RuntimeConfig.AppInternalName + " schemes'"); return 1; }
            }
            else if (!Confirm(Translations.Get("reset_warn_text")))
                return 1;

            foreach (SoundEvent soundEvent in SoundEvent.GetAll())
                SoundScheme.CopyDefault(soundEvent, source);
            SchemeMeta.ResetAll();
            if (source != null)
            {
                SchemeMeta.Name = source.ToString();
                SchemeMeta.Author = "";
                SchemeMeta.About = "";
            }
            SchemeMeta.Thumbnail = null;
            SoundScheme.Apply(SoundScheme.GetSchemeSoundManager(), Settings.MissingSoundUseDefault);
            return 0;
        }

        private static int CmdImport(string[] args)
        {
            if (args.Length < 1) { Error("usage: import <file>"); return 1; }
            if (!File.Exists(args[0])) { Error("File not found: " + args[0]); return 1; }
            SoundArchive.Import(Path.GetFullPath(args[0]));
            Console.WriteLine(Translations.Get("cli_scheme_imported"));
            return 0;
        }

        private static int CmdExport(string[] args)
        {
            if (args.Length < 1) { Error("usage: export <file>"); return 1; }
            string target = Path.GetFullPath(args[0]);
            if (target.ToLowerInvariant().EndsWith("." + SoundArchiveThemepack.FileExtension))
                SoundArchiveThemepack.Export(target);
            else
                SoundArchive.Export(target);
            Console.WriteLine(target);
            return 0;
        }

        private static int CmdMeta(string[] args)
        {
            SchemeMeta.ReloadFromDisk();
            if (args.Length == 0)
            {
                Console.WriteLine("name=" + SchemeMeta.Name);
                Console.WriteLine("author=" + SchemeMeta.Author);
                Console.WriteLine("about=" + SchemeMeta.About);
                Console.WriteLine("image=" + (File.Exists(SchemeMeta.SchemeImageFilePath) ? SchemeMeta.SchemeImageFilePath : ""));
                return 0;
            }
            foreach (string arg in args)
            {
                int eq = arg.IndexOf('=');
                if (eq <= 0) { Error("usage: meta [name=X] [author=X] [about=X] [image=path|none]"); return 1; }
                string key = arg.Substring(0, eq).ToLowerInvariant();
                string value = arg.Substring(eq + 1);
                switch (key)
                {
                    case "name": SchemeMeta.Name = value; break;
                    case "author": SchemeMeta.Author = value; break;
                    case "about": SchemeMeta.About = value; break;
                    case "image":
                        if (value == "" || value.ToLowerInvariant() == "none")
                            SchemeMeta.Thumbnail = null;
                        else
                            SchemeMeta.Thumbnail = System.Drawing.Image.FromFile(value);
                        break;
                    default: Error("Unknown field: " + key); return 1;
                }
            }
            return 0;
        }

        private static int CmdSchemes()
        {
            foreach (SoundScheme scheme in SoundScheme.GetSchemeList())
                if (scheme.ToString() != RuntimeConfig.AppDisplayName && scheme.ToString() != ".None")
                    Console.WriteLine(scheme.ToString());
            return 0;
        }

        private static int CmdApply()
        {
            SoundScheme.Setup();
            SoundScheme.Apply(SoundScheme.GetSchemeSoundManager(), Settings.MissingSoundUseDefault);
            return 0;
        }

        /// <summary>
        /// Download sound schemes from the schemes repository (former DownloadSchemes.exe)
        /// </summary>
        private static int CmdDownload(string[] args)
        {
            try
            {
                // Enable TLS 1.0, 1.1, 1.2, by default .NET 4.0 will enable TLS 1.0 only
                ServicePointManager.SecurityProtocol |= (SecurityProtocolType)(0xc0 | 0x300 | 0xc00);
            }
            catch (NotSupportedException)
            {
                Error(Translations.Get("download_schemes_no_tls_text"));
                Console.WriteLine(RuntimeConfig.SchemesRepositoryUrl);
                return 1;
            }

            List<string> urls;
            try
            {
                urls = FetchSchemeList().ToList();
            }
            catch (Exception e)
            {
                Error(Translations.Get("download_schemes_failed_text"));
                Error(e.Message);
                Console.WriteLine(RuntimeConfig.SchemesRepositoryUrl);
                return 1;
            }

            if (args.Length == 0)
            {
                Console.WriteLine(Translations.Get("download_schemes_selection_text"));
                Console.WriteLine();
                string recommended = SchemesPerNtVersion.ContainsKey(RuntimeConfig.WindowsNtVersion)
                    ? SchemesPerNtVersion[RuntimeConfig.WindowsNtVersion] : null;
                foreach (string url in urls)
                {
                    string name = Path.GetFileName(url);
                    bool present = File.Exists(Path.Combine(RuntimeConfig.SchemesFolder, name));
                    Console.WriteLine("  {0} {1}{2}",
                        present ? "[downloaded]" : "[          ]",
                        name,
                        name == recommended ? "  <- " + Translations.Get("cli_download_recommended") : "");
                }
                Console.WriteLine();
                Console.WriteLine("usage: " + RuntimeConfig.AppInternalName + " download <all|name...>");
                return 0;
            }

            List<string> selected = new List<string>();
            if (args.Length == 1 && args[0].ToLowerInvariant() == "all")
            {
                selected.AddRange(urls);
            }
            else
            {
                foreach (string arg in args)
                {
                    string match = urls.FirstOrDefault(u =>
                        String.Equals(Path.GetFileName(u), arg, StringComparison.OrdinalIgnoreCase)
                        || String.Equals(Path.GetFileNameWithoutExtension(u), arg, StringComparison.OrdinalIgnoreCase));
                    if (match == null) { Error("Unknown scheme: " + arg); return 1; }
                    selected.Add(match);
                }
            }

            if (!Directory.Exists(RuntimeConfig.SchemesFolder))
                Directory.CreateDirectory(RuntimeConfig.SchemesFolder);

            string lastDownloaded = null;
            using (WebClient client = new WebClient())
            {
                foreach (string url in selected)
                {
                    string localPath = Path.Combine(RuntimeConfig.SchemesFolder, Path.GetFileName(url));
                    if (File.Exists(localPath))
                    {
                        Console.WriteLine("[skip] " + Path.GetFileName(url));
                        continue;
                    }
                    Console.Write("[ dl ] " + Path.GetFileName(url) + " ... ");
                    client.DownloadFile(url, localPath);
                    Console.WriteLine("ok");
                    lastDownloaded = localPath;
                }
            }

            // Offer to apply: single downloaded scheme, or the one matching the OS
            string schemeFile = (selected.Count == 1) ? Path.Combine(RuntimeConfig.SchemesFolder, Path.GetFileName(selected[0])) : null;
            if (SchemesPerNtVersion.ContainsKey(RuntimeConfig.WindowsNtVersion))
            {
                string osScheme = Path.Combine(RuntimeConfig.SchemesFolder, SchemesPerNtVersion[RuntimeConfig.WindowsNtVersion]);
                if (selected.Any(u => Path.GetFileName(u) == SchemesPerNtVersion[RuntimeConfig.WindowsNtVersion]) && File.Exists(osScheme))
                    schemeFile = osScheme;
            }
            if (schemeFile != null && File.Exists(schemeFile)
                && Confirm(Translations.Get("scheme_load_prompt_text") + " " + Path.GetFileName(schemeFile)))
            {
                SoundArchive.Import(schemeFile);
                Console.WriteLine(Translations.Get("cli_scheme_imported"));
            }
            return 0;
        }

        /// <summary>
        /// Retrieve the list of sound scheme URLs from the GitHub repository, with 1-hour caching due to API rate-limit.
        /// </summary>
        private static IEnumerable<string> FetchSchemeList()
        {
            if (File.Exists(SchemesListTempFile) && File.GetLastWriteTime(SchemesListTempFile) >= DateTime.Now.AddHours(-1))
            {
                return File.ReadAllLines(SchemesListTempFile);
            }
            else
            {
                IEnumerable<string> urls = GitHubApi.ListFilesInRepo(RuntimeConfig.ProjectRepositoryUsername, RuntimeConfig.SchemesRepositoryName, "/", true).Where(item => item.EndsWith(".ths"));
                File.WriteAllLines(SchemesListTempFile, urls.ToArray());
                return urls;
            }
        }
    }
}
