using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using SharpTools;

namespace SoundManager
{
    /// <summary>
    /// Registration of the background sound player (SoundManagerBg.exe) on system startup.
    /// Split from BgSoundPlayer so the console app can manage registration without
    /// referencing the hidden window itself.
    /// Task Scheduler COM API is accessed through late binding (no build-time type library).
    /// By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
    /// </summary>
    static class BgSoundPlayerSetup
    {
        // Task Scheduler 2.0 COM API constants (taskschd.h)
        private const int TASK_TRIGGER_LOGON = 9;           // _TASK_TRIGGER_TYPE2
        private const int TASK_ACTION_EXEC = 0;             // _TASK_ACTION_TYPE
        private const int TASK_CREATE_OR_UPDATE = 6;        // _TASK_CREATION
        private const int TASK_LOGON_INTERACTIVE_TOKEN = 3; // _TASK_LOGON_TYPE
        private const uint HRESULT_FILE_NOT_FOUND = 0x80070002u;

        private static readonly RegistryKey SystemStartup = Registry.CurrentUser.OpenSubKey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", true);
        private static readonly RegistryKey StartupDelay = Registry.CurrentUser.CreateSubKey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize");
        private const string StartupDelay_StartupDelayInMSec = "StartupDelayInMSec";

        private static readonly string SidCurrentUser = System.Security.Principal.WindowsIdentity.GetCurrent().User.Value;
        private static readonly string ScheduledTaskBaseName = RuntimeConfig.AppInternalName;
        private static readonly string ScheduledTaskNameCurrentUser = String.Concat(ScheduledTaskBaseName, "-", SidCurrentUser);
        private static readonly string StartupCommandExe = RuntimeConfig.SoundManagerBgExe;
        private static readonly string StartupCommand = String.Concat("\"", StartupCommandExe, "\"");

        /// <summary>
        /// Instantiate the Task Scheduler 2.0 COM service for late-bound calls
        /// </summary>
        private static dynamic NewTaskService()
        {
            return Activator.CreateInstance(Type.GetTypeFromProgID("Schedule.Service"));
        }

        /// <summary>
        /// Check whether a COMException means "file not found", which late-bound
        /// Task Scheduler calls may surface instead of FileNotFoundException
        /// </summary>
        private static bool IsFileNotFound(COMException e)
        {
            return (uint)e.ErrorCode == HRESULT_FILE_NOT_FOUND;
        }

        /// <summary>
        /// Check if the background sound player is required for the current Windows version
        /// </summary>
        public static bool RequiredForThisWindowsVersion
        {
            get
            {
                return WindowsVersion.IsAtLeast8;
            }
        }

        /// <summary>
        /// Check registration of the background sound player on system startup
        /// </summary>
        /// <returns>TRUE when registered on system startup</returns>
        public static bool IsRegisteredForStartup()
        {
            bool registryKeyPresent = (StartupCommand == (SystemStartup.GetValue(RuntimeConfig.AppInternalName) as string));
            bool taskPresent = false;

            try
            {
                dynamic ts = NewTaskService();
                ts.Connect();
                taskPresent = (ts.GetFolder("\\").GetTask(ScheduledTaskNameCurrentUser) != null);
            }
            catch (FileNotFoundException) { /* Task not present */ }
            catch (COMException e) { if (!IsFileNotFound(e)) throw; /* Task not present */ }
            catch (UnauthorizedAccessException) { /* Task is present but wrong permissions */ }

            return registryKeyPresent || taskPresent;
        }

        /// <summary>
        /// Register, unregister the background sound player on system startup
        /// </summary>
        /// <param name="registered">TRUE to register for startup, FALSE to unregister</param>
        /// <param name="interactive">TRUE when user changes the setting interactively, FALSE during setup/uninstall</param>
        public static void SetRegisteredForStartup(bool registered, bool interactive = false)
        {
            dynamic ts = NewTaskService();
            ts.Connect();

            if (registered)
            {
                //Create scheduled task - Runs sooner on logon compared to registry keys
                string taskSecurityDescriptor = String.Concat("O:", SidCurrentUser, "D:(A;;FA;;;", SidCurrentUser, ")");
                dynamic task = ts.NewTask(0);
                dynamic trigger = task.Triggers.Create(TASK_TRIGGER_LOGON);
                trigger.UserId = SidCurrentUser;
                dynamic action = task.Actions.Create(TASK_ACTION_EXEC);
                action.Path = StartupCommandExe;
                action.Arguments = "";
                task.Settings.DisallowStartIfOnBatteries = false;
                task.Settings.StopIfGoingOnBatteries = false;
                task.Settings.ExecutionTimeLimit = "PT0S";
                task.Settings.Priority = 5; // Normal

                try
                {
                    ts.GetFolder("\\").RegisterTaskDefinition(
                        ScheduledTaskNameCurrentUser,
                        task,
                        TASK_CREATE_OR_UPDATE,
                        null,
                        null,
                        TASK_LOGON_INTERACTIVE_TOKEN,
                        taskSecurityDescriptor
                    );
                }
                catch (UnauthorizedAccessException)
                {
                    //Not allowed to create the scheduled task because it already exists with wrong permissions
                    //Should not happen since tasks are per-user, but better warn the user.
                    if (interactive)
                        throw;
                }
            }
            else
            {
                //Remove scheduled task for the current user
                try { ts.GetFolder("\\").DeleteTask(ScheduledTaskNameCurrentUser, 0); }
                catch (FileNotFoundException) { /* Task was not present */ }
                catch (COMException e) { if (!IsFileNotFound(e)) throw; /* Task was not present */ }
                catch (UnauthorizedAccessException) /* Insufficient privileges */
                {
                    //Should not happen since tasks are per-user, but better warn the user.
                    if (interactive)
                        throw;
                }

                //Also remove tasks for other users when performing Uninstall as Admin
                if (!interactive && FileSystemAdmin.IsAdmin())
                {
                    dynamic tasks = ts.GetFolder("\\").GetTasks(0);
                    for (int i = 1; i <= tasks.Count; i++)
                    {
                        string taskPath = tasks[i].Path;
                        if (taskPath.StartsWith("\\" + ScheduledTaskBaseName))
                            ts.GetFolder("\\").DeleteTask(taskPath.Substring(1), 0);
                    }
                }
            }

            //Remove registry keys set by previous versions of this program
            SystemStartup.DeleteValue(RuntimeConfig.AppInternalName, false);
            StartupDelay.DeleteValue(StartupDelay_StartupDelayInMSec, false);

            //Attempt to remove generic scheduled task set by previous versions of this program
            try { ts.GetFolder("\\").DeleteTask(RuntimeConfig.AppInternalName, 0); }
            catch (FileNotFoundException) { /* Task was not present */ }
            catch (COMException e) { if (!IsFileNotFound(e)) throw; /* Task was not present */ }
            catch (UnauthorizedAccessException) { /* Insufficient privileges */ }
        }

        /// <summary>
        /// Determine whether the built-in system startup sound should be enabled and apply the appropriate setting
        /// </summary>
        public static void UpdateStartupSoundSetting()
        {
            if (Settings.PatchStartupSound)
            {
                // When patching the startup sound, we want it to play regardless of other settings
                SystemStartupSound.Enabled = true;
            }
            else if (IsRegisteredForStartup())
            {
                // When using the background sound player and NOT patching the startup sound, mute the built-in startup sound and play the custom one using BgSoundPlayer
                SystemStartupSound.Enabled = false;
            }
            else
            {
                // When using none of the above, restore the system default behavior regarding startup sound
                SystemStartupSound.Enabled = SystemStartupSound.DefaultEnabled;
            }
        }
    }
}
