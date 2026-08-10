using System;
using System.Windows.Forms;
using System.Linq;
using SharpTools;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace SoundManager
{
    /// <summary>
    /// Application allowing to create, load and share Windows sound schemes
    /// By ORelio - (c) 2009-2026 - Available under the CDDL-1.0 license
    /// </summary>
    /// <remarks>
    /// Modified by ayanami770 (2026): the WinForms GUI is replaced by a menuconfig-style
    /// TUI (SmTui) and CLI subcommands (Cli); the background sound player lives in the
    /// dedicated SoundManagerBg.exe - CDDL-1.0
    /// </remarks>
    public static class Program
    {
        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]
        static int Main(string[] args)
        {
            if (RuntimeConfig.Version.ToLowerInvariant().Contains("test") || args.Contains(RuntimeConfig.CmdArgumentDebug))
                ExceptionLogger.StartLogging(Application.ExecutablePath + ".debug.log", RuntimeConfig.Version);

            try { Console.OutputEncoding = Encoding.UTF8; }
            catch { /* Very old consoles may refuse UTF-8; keep the default */ }

            string importFile = null;

            if (args.Length > 0)
            {
                switch (args[0].ToLowerInvariant())
                {
                    case RuntimeConfig.CmdArgumentSetup:
                        Setup(forceResetSounds: false, systemIntegration: true, offerImportCurrentScheme: false);
                        return 0;

                    case RuntimeConfig.CmdArgumentUninstall:
                        Uninstall();
                        return 0;

                    case RuntimeConfig.CmdArgumentBgSoundPlayer:
                        // Compatibility with scheduled tasks registered by previous versions:
                        // the background player is now a dedicated windowless executable
                        if (File.Exists(RuntimeConfig.SoundManagerBgExe))
                            Process.Start(RuntimeConfig.SoundManagerBgExe);
                        return 0;

                    case RuntimeConfig.CmdArgumentDebug:
                        // Already handled above
                        break;

                    default:
                        if (File.Exists(args[0]))
                        {
                            importFile = args[0];
                        }
                        else
                        {
                            return Cli.Run(args);
                        }
                        break;
                }
            }

            if (ImageresPatcher.IsPatchingPossible && !FileSystemAdmin.IsAdmin() && Settings.PatchStartupSound)
            {
                try
                {
                    ProcessStartInfo startInfo = new ProcessStartInfo(Application.ExecutablePath);
                    if (args.Length > 0)
                        startInfo.Arguments = "\"" + args[0] + "\"";
                    startInfo.Verb = "runas";
                    Process.Start(startInfo);
                    return 0;
                }
                catch
                {
                    Cli.Error(Translations.Get("startup_patch_not_elevated_text"));
                }
            }

            if (importFile != null)
            {
                // Sound scheme file passed as argument (double-clicked .ths file)
                Setup(forceResetSounds: false, systemIntegration: false, offerImportCurrentScheme: false);
                if (Cli.Confirm(Translations.Get("scheme_load_prompt_text") + " " + Path.GetFileName(importFile)))
                {
                    try
                    {
                        SoundArchive.Import(importFile);
                        Console.WriteLine(Translations.Get("cli_scheme_imported"));
                    }
                    catch (Exception importException)
                    {
                        Cli.Error(Translations.Get("scheme_load_failed_text") + " " + importException.Message);
                        return 1;
                    }
                }
                return 0;
            }

            Setup(forceResetSounds: false, systemIntegration: false, offerImportCurrentScheme: true);
            return SmTui.RunConfigurator();
        }

        /// <summary>
        /// Setup will create and apply the SoundManager sound scheme, create data directory
        /// </summary>
        /// <param name="forceResetSounds">Also reset all sounds to their default values</param>
        /// <param name="systemIntegration">Also setup maximum system integration</param>
        /// <param name="offerImportCurrentScheme">Offer to import the active scheme if changed externally</param>
        public static void Setup(bool forceResetSounds, bool systemIntegration, bool offerImportCurrentScheme)
        {
            bool createDataDir = !Directory.Exists(RuntimeConfig.LocalDataFolder);

            if (createDataDir)
            {
                Directory.CreateDirectory(RuntimeConfig.LocalDataFolder);
                Directory.CreateDirectory(SoundEvent.DataDirectory);
            }

            SoundScheme activeScheme = SoundScheme.GetActiveScheme();
            if (offerImportCurrentScheme && SoundScheme.AlreadySetup() && activeScheme != null && !activeScheme.IsSchemeManager)
            {
                if (Cli.Confirm(Translations.Get("auto_import_offer_text") + " " + activeScheme.ToString()))
                {
                    forceResetSounds = true;
                }
            }

            SoundScheme.Setup();

            if (forceResetSounds || createDataDir)
            {
                SchemeMeta.ResetAll();
                if (activeScheme != null && !activeScheme.IsDefault)
                {
                    SchemeMeta.Name = activeScheme.ToString();
                    SchemeMeta.Author = "";
                    SchemeMeta.About = "";
                }
                foreach (SoundEvent soundEvent in SoundEvent.GetAll())
                    SoundScheme.CopyDefault(soundEvent, activeScheme);
            }

            SoundScheme.Apply(SoundScheme.GetSchemeSoundManager(), true);

            if (systemIntegration)
            {
                SoundArchive.AssocFiles();
                if (BgSoundPlayerSetup.RequiredForThisWindowsVersion)
                {
                    SystemStartupSound.Enabled = false;
                    BgSoundPlayerSetup.SetRegisteredForStartup(true);
                    if (File.Exists(RuntimeConfig.SoundManagerBgExe))
                        Process.Start(RuntimeConfig.SoundManagerBgExe);
                }
            }
        }

        /// <summary>
        /// Uninstall will remove application data from the user directory, sound scheme from the registry, and disable all system integration
        /// </summary>
        public static void Uninstall()
        {
            if (BgSoundPlayerSetup.IsRegisteredForStartup())
            {
                SystemStartupSound.Enabled = SystemStartupSound.DefaultEnabled;
                BgSoundPlayerSetup.SetRegisteredForStartup(false);
                foreach (Process process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(RuntimeConfig.SoundManagerBgExe)))
                    process.Kill();
                foreach (Process process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(Application.ExecutablePath)))
                    if (process.Id != Process.GetCurrentProcess().Id)
                        process.Kill();
            }
            SoundArchive.UnAssocFiles();
            SoundScheme.Uninstall();
            if (Directory.Exists(SoundEvent.DataDirectory))
                Directory.Delete(SoundEvent.DataDirectory, true);
            if (Directory.Exists(RuntimeConfig.LocalDataFolder))
                Directory.Delete(RuntimeConfig.LocalDataFolder, true);
        }
    }
}
