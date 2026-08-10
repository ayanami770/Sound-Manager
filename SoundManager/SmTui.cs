using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using SharpTools;

namespace SoundManager
{
    /// <summary>
    /// Menuconfig-style configuration TUI, bridging to the vendored Universal-TUI
    /// engine inside SmNative.dll. Builds the spec describing the configuration
    /// tree from current application state, runs the interactive editor, and
    /// applies the edited values back (replacing the former FormMain GUI).
    /// By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
    /// </summary>
    static class SmTui
    {
        [DllImport("SmNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int smtui_run(byte[] specUtf8, byte[] outUtf8, int outCap);

        private const int E_TUI_NOTTY = -20;

        /// <summary>
        /// Run the configuration TUI and apply changes if the user saved.
        /// </summary>
        /// <returns>Process exit code</returns>
        public static int RunConfigurator()
        {
            SoundEvent[] events = SoundEvent.GetAll();
            string scratchConfig = Path.Combine(RuntimeConfig.LocalDataFolder, "TuiSession.config");
            if (File.Exists(scratchConfig))
                File.Delete(scratchConfig);

            StringBuilder spec = new StringBuilder();
            Kv(spec, "config", scratchConfig);
            Kv(spec, "backtitle", RuntimeConfig.AppDisplayName + " " + RuntimeConfig.Version + " - " + Translations.Get("tui_backtitle_hint"));
            Kv(spec, "title", RuntimeConfig.AppDisplayName);
            Kv(spec, "instructions", Translations.Get("tui_instructions"));
            Kv(spec, "group.events", Translations.Get("tui_menu_events"));
            Kv(spec, "group.meta", Translations.Get("tui_menu_meta"));
            Kv(spec, "group.settings", Translations.Get("tui_menu_settings"));
            Kv(spec, "label.file", Translations.Get("tui_sound_file"));

            SchemeMeta.ReloadFromDisk();
            Kv(spec, "meta.name.label", Translations.Get("meta_name"));
            Kv(spec, "meta.name.value", SchemeMeta.Name);
            Kv(spec, "meta.author.label", Translations.Get("meta_author"));
            Kv(spec, "meta.author.value", SchemeMeta.Author);
            Kv(spec, "meta.about.label", Translations.Get("meta_about"));
            Kv(spec, "meta.about.value", SchemeMeta.About);
            Kv(spec, "meta.image.label", Translations.Get("tui_meta_image"));
            Kv(spec, "meta.image.value", File.Exists(SchemeMeta.SchemeImageFilePath) ? SchemeMeta.SchemeImageFilePath : "");

            KvSetting(spec, "patch", Translations.Get("check_box_imageres_patch"), null,
                ImageresPatcher.IsPatchingPossible && Settings.PatchStartupSound, ImageresPatcher.IsPatchingPossible);
            KvSetting(spec, "bgplayer", Translations.Get("check_box_bg_sound_player"), null,
                BgSoundPlayerSetup.RequiredForThisWindowsVersion && BgSoundPlayerSetup.IsRegisteredForStartup(),
                BgSoundPlayerSetup.RequiredForThisWindowsVersion);
            KvSetting(spec, "assoc", Translations.Get("check_box_file_assoc"), null,
                SoundArchive.FileAssociation, true);
            KvSetting(spec, "missing", Translations.Get("check_box_reset_missing_on_load"), null,
                Settings.MissingSoundUseDefault, true);
            KvSetting(spec, "convert", Translations.Get("tui_check_convert_proprietary"), null,
                Settings.ConvertProprietaryFiles, true);
            KvSetting(spec, "preferstartup", Translations.Get("tui_check_prefer_startup"), null,
                Settings.PreferStartupSoundOnLogon, true);

            Kv(spec, "event.count", events.Length.ToString());
            for (int i = 0; i < events.Length; i++)
            {
                string prefix = "event." + i + ".";
                Kv(spec, prefix + "sym", events[i].InternalName.ToUpperInvariant());
                Kv(spec, prefix + "name", events[i].DisplayName);
                Kv(spec, prefix + "help", events[i].Description);
                Kv(spec, prefix + "enabled", events[i].Disabled ? "0" : "1");
                Kv(spec, prefix + "file", File.Exists(events[i].FilePath) ? events[i].FilePath : "");
            }

            byte[] specBytes = NulTerminated(spec.ToString());
            byte[] outBuf = new byte[131072];

            SoundScheme.UiOpen = true;
            SoundScheme.Setup();
            SoundScheme.Apply(SoundScheme.GetSchemeSoundManager(), Settings.MissingSoundUseDefault);
            int result;
            try
            {
                result = smtui_run(specBytes, outBuf, outBuf.Length);
            }
            finally
            {
                SoundScheme.UiOpen = false;
            }

            if (File.Exists(scratchConfig))
                File.Delete(scratchConfig);

            if (result == E_TUI_NOTTY)
            {
                Console.Error.WriteLine(Translations.Get("tui_needs_console"));
                return 1;
            }
            if (result < 0)
            {
                Console.Error.WriteLine("TUI error " + result);
                return 1;
            }

            Dictionary<string, string> values = ParseResult(outBuf);
            if (result == 1 && values.ContainsKey("saved") && values["saved"] == "1")
            {
                ApplyChanges(events, values);
                Console.WriteLine(Translations.Get("tui_saved"));
            }
            else
            {
                Console.WriteLine(Translations.Get("tui_not_saved"));
            }

            // Restore normal scheme state after closing the UI (same as FormMain did on close)
            SoundScheme.Setup();
            SoundScheme.Apply(SoundScheme.GetSchemeSoundManager(), Settings.MissingSoundUseDefault);
            return 0;
        }

        /// <summary>
        /// Apply edited values, mirroring the behavior of the former FormMain handlers
        /// </summary>
        private static void ApplyChanges(SoundEvent[] events, Dictionary<string, string> values)
        {
            // Scheme metadata
            string metaName = GetValue(values, "meta.name");
            string metaAuthor = GetValue(values, "meta.author");
            string metaAbout = GetValue(values, "meta.about");
            string metaImage = GetValue(values, "meta.image");
            if (metaName != null && metaName != SchemeMeta.Name) SchemeMeta.Name = metaName;
            if (metaAuthor != null && metaAuthor != SchemeMeta.Author) SchemeMeta.Author = metaAuthor;
            if (metaAbout != null && metaAbout != SchemeMeta.About) SchemeMeta.About = metaAbout;
            if (metaImage != null)
            {
                string currentImage = File.Exists(SchemeMeta.SchemeImageFilePath) ? SchemeMeta.SchemeImageFilePath : "";
                if (metaImage != currentImage)
                {
                    if (metaImage == "")
                    {
                        SchemeMeta.Thumbnail = null;
                    }
                    else if (File.Exists(metaImage))
                    {
                        try { SchemeMeta.Thumbnail = Image.FromFile(metaImage); }
                        catch (Exception e) { Cli.Error(Translations.Get("image_load_failed_text") + " " + e.Message); }
                    }
                    else Cli.Error(Translations.Get("image_load_failed_text") + " " + metaImage);
                }
            }

            // Sound events: enable/disable and file changes
            for (int i = 0; i < events.Length; i++)
            {
                string enabled = GetValue(values, "event." + i + ".enabled");
                string file = GetValue(values, "event." + i + ".file");

                if (enabled != null)
                {
                    bool disabled = (enabled == "0");
                    if (disabled != events[i].Disabled)
                        events[i].Disabled = disabled;
                }

                if (file != null)
                {
                    string currentFile = File.Exists(events[i].FilePath) ? events[i].FilePath : "";
                    if (file != currentFile)
                    {
                        try
                        {
                            if (file == "")
                            {
                                SoundScheme.Remove(events[i]);
                            }
                            else if (File.Exists(file))
                            {
                                SoundScheme.Update(events[i], file);
                            }
                            else
                            {
                                Cli.Error(Translations.Get("sound_load_failed_text") + " " + file);
                            }
                        }
                        catch (Exception e)
                        {
                            Cli.Error(Translations.Get("sound_load_failed_text") + " " + events[i].DisplayName + ": " + e.Message);
                        }
                    }
                }
            }

            // Settings toggles
            string v;
            if ((v = GetValue(values, "set.missing")) != null)
                Settings.MissingSoundUseDefault = (v == "1");
            if ((v = GetValue(values, "set.convert")) != null)
                Settings.ConvertProprietaryFiles = (v == "1");
            if ((v = GetValue(values, "set.preferstartup")) != null)
                Settings.PreferStartupSoundOnLogon = (v == "1");

            if ((v = GetValue(values, "set.assoc")) != null)
            {
                bool assoc = (v == "1");
                if (assoc != SoundArchive.FileAssociation)
                {
                    if (assoc) SoundArchive.AssocFiles();
                    else SoundArchive.UnAssocFiles();
                }
            }

            if ((v = GetValue(values, "set.patch")) != null)
            {
                bool patch = (v == "1");
                if (patch != Settings.PatchStartupSound)
                {
                    if (patch && ImageresPatcher.IsPatchingNotRecommended
                        && !Cli.Confirm(Translations.Get("startup_patch_not_recommended_text")))
                    {
                        patch = Settings.PatchStartupSound;
                    }
                    else if (FileSystemAdmin.IsAdmin())
                    {
                        try
                        {
                            if (patch)
                            {
                                SoundEvent startupSound = SoundEvent.Get(SoundEvent.EventType.Startup);
                                if (File.Exists(startupSound.FilePath))
                                    ImageresPatcher.Patch(startupSound.FilePath);
                            }
                            else ImageresPatcher.Restore();
                        }
                        catch (Exception e) { Cli.Error(e.Message); }
                    }
                    else if (patch)
                    {
                        Cli.Error(Translations.Get("startup_patch_not_elevated_text"));
                    }
                    Settings.PatchStartupSound = patch;
                }
            }

            if ((v = GetValue(values, "set.bgplayer")) != null)
            {
                bool bgplayer = (v == "1");
                if (bgplayer != BgSoundPlayerSetup.IsRegisteredForStartup())
                {
                    try { BgSoundPlayerSetup.SetRegisteredForStartup(bgplayer, true); }
                    catch (Exception e) { Cli.Error(e.Message); }
                }
            }

            BgSoundPlayerSetup.UpdateStartupSoundSetting();
            Settings.Save();
        }

        private static string GetValue(Dictionary<string, string> values, string key)
        {
            string v;
            return values.TryGetValue(key, out v) ? v : null;
        }

        /// <summary>
        /// Append one spec line; values must stay single-line
        /// </summary>
        private static void Kv(StringBuilder spec, string key, string value)
        {
            if (value == null)
                value = "";
            spec.Append(key).Append('=').Append(value.Replace("\r", " ").Replace("\n", " ")).Append('\n');
        }

        private static void KvSetting(StringBuilder spec, string key, string label, string help, bool value, bool show)
        {
            Kv(spec, "set." + key + ".label", label);
            if (help != null)
                Kv(spec, "set." + key + ".help", help);
            Kv(spec, "set." + key + ".value", value ? "1" : "0");
            Kv(spec, "set." + key + ".show", show ? "1" : "0");
        }

        private static byte[] NulTerminated(string s)
        {
            byte[] utf8 = Encoding.UTF8.GetBytes(s);
            byte[] result = new byte[utf8.Length + 1];
            Array.Copy(utf8, result, utf8.Length);
            return result;
        }

        private static Dictionary<string, string> ParseResult(byte[] outBuf)
        {
            int len = Array.IndexOf<byte>(outBuf, 0);
            if (len < 0)
                len = outBuf.Length;
            string text = Encoding.UTF8.GetString(outBuf, 0, len);
            Dictionary<string, string> values = new Dictionary<string, string>();
            foreach (string line in text.Split('\n'))
            {
                int eq = line.IndexOf('=');
                if (eq > 0)
                    values[line.Substring(0, eq)] = line.Substring(eq + 1);
            }
            return values;
        }
    }
}
