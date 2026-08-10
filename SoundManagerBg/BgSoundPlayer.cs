using System;
using System.Windows.Forms;
using System.Runtime.InteropServices;
using System.Media;
using System.IO;
using System.Drawing;
using System.Threading;
using System.Security.AccessControl;
using Microsoft.Win32;
using SharpTools;

namespace SoundManager
{
    /// <summary>
    /// Hidden form for playing startup, login, logoff, lock, unlock, shutdown sound events.
    /// Builtin playback of these sound events was removed in Windows 8 so we need a background process for that.
    /// Although built-in startup sound can be played on Windows 10+, it is not modifiable and other events are still missing.
    /// This class is NOT useful on Windows XP/Vista/7 and not compatible with Windows XP (No ShutdownBlockReason API, older Task Scheduler API).
    /// </summary>
    /// <remarks>
    /// Modified by ayanami770 (2026): moved to the dedicated SoundManagerBg executable;
    /// startup registration lives in BgSoundPlayerSetup - CDDL-1.0
    /// </remarks>
    public class BgSoundPlayer : Form
    {
        private static readonly string LastBootFile = Path.Combine(RuntimeConfig.LocalDataFolder, "LastBootTime.ini");

        [DllImport("kernel32")]
        private static extern UInt64 GetTickCount64();

        [DllImport("user32.dll")]
        private static extern bool ShutdownBlockReasonCreate(IntPtr hWnd, [MarshalAs(UnmanagedType.LPWStr)] string pwszReason);

        [DllImport("user32.dll")]
        private static extern bool ShutdownBlockReasonDestroy(IntPtr wndHandle);

        /// <summary>
        /// Get boot unix timestamp in seconds
        /// </summary>
        private static long GetBootTimestamp()
        {
            long remainder;
            return Math.DivRem(((long)DateTime.UtcNow.Subtract(new DateTime(1970, 1, 1)).TotalMilliseconds - (long)GetTickCount64()), 1000, out remainder);
        }

        /// <summary>
        /// Check if the user has not reached the Desktop yet
        /// </summary>
        private static bool IsScreenLocked()
        {
            switch (WindowManager.GetActiveWindowExeName().ToLower())
            {
                case "idle.exe":
                case "lockapp.exe":
                case "logonui.exe":
                    return true;
                default:
                    return false;
            }
        }

        /// <summary>
        /// Play the specified system event sound.
        /// The sound currently associated with the event is played, which is not necessarily from the SoundManager scheme.
        /// </summary>
        /// <param name="soundEvent">System event to play</param>
        private static void PlaySound(SoundEvent soundEvent)
        {
            if (soundEvent != null)
            {
                string soundFile = SoundScheme.GetCurrentFile(soundEvent);
                if (soundFile != null && File.Exists(soundFile))
                {
                    try
                    {
                        new SoundPlayer(soundFile).PlaySync();
                    }
                    catch (InvalidOperationException) { /* Invalid WAV file */ }
                }
            }
        }

        private SoundEvent soundStartup = SoundEvent.Get(SoundEvent.EventType.Startup);
        private SoundEvent soundShutdown = SoundEvent.Get(SoundEvent.EventType.Shutdown);
        private SoundEvent soundLogon = SoundEvent.Get(SoundEvent.EventType.Logon);
        private SoundEvent soundLogoff = SoundEvent.Get(SoundEvent.EventType.Logoff);

        /// <summary>
        /// Instantiate a new Background Sound Player.
        /// Will play the startup/logon sound and create a hidden window, which is required by the ShutdownBlockReason API.
        /// </summary>
        public BgSoundPlayer()
        {
            //this.Text = RuntimeConfig.AppDisplayName; // Window title disabled, see below (issue #8)
            this.Icon = IconExtractor.ExtractAssociatedIcon(Application.ExecutablePath);

            // Hide the window in such a way Windows will still consider it eligible for ShutdownBlockReason
            this.FormBorderStyle = FormBorderStyle.FixedToolWindow; // Remove from Alt+Tab
            this.ShowInTaskbar = false;
            this.StartPosition = FormStartPosition.Manual;
            this.Location = new System.Drawing.Point(-9999, -9999);
            this.Size = new System.Drawing.Size(1, 1);
            this.GotFocus += new EventHandler(WindowFocused); // Evade focus (issue #8)
            this.Text = "�"; // Avoid screen readers saying the window title loud (issue #8)
            this.LocationChanged += new EventHandler(OnLocationChanged); // Relocate on screen layout change
            this.Resize += new EventHandler(OnResize); // Make sure the window is not automatically minimized

            // Determine system startup time
            string bootTime = GetBootTimestamp().ToString();
            bootTime = bootTime.Substring(0, bootTime.Length - 1) + '0';
            string lastBootTime = "";
            if (File.Exists(LastBootFile))
                lastBootTime = File.ReadAllText(LastBootFile);

            // Determine default logon sound to play
            SoundEvent soundToPlay =
                Settings.PreferStartupSoundOnLogon && File.Exists(soundStartup.FilePath)
                    ? soundStartup
                    : soundLogon;

            // Handle case where system has rebooted - startup sound instead of logon?
            if (bootTime != lastBootTime)
            {
                File.WriteAllText(LastBootFile, bootTime);
                if (!SystemStartupSound.Enabled)
                {
                    // We need to emulate the startup sound
                    soundToPlay = soundStartup;
                }
                else // The system itself plays the startup sound
                {
                    if (!AccountProperties.HasPassword(Environment.UserName) || AccountProperties.HasAutoLogon(Environment.UserName))
                    {
                        // Built-in system startup sound plays on logon when the account logs automatically
                        soundToPlay = null;
                    }
                    else
                    {
                        // Built-in system startup sound already played on the logon screen
                        // Once the user logs on after authenticating, the logon sound is supposed to play
                        soundToPlay = soundLogon;
                    }
                }
            }

            // Play sound event?
            if (soundToPlay != null)
            {
                while (IsScreenLocked())
                    Thread.Sleep(100);
                PlaySound(soundToPlay);
            }

            SystemEvents.SessionEnding += new SessionEndingEventHandler(SystemEvents_SessionEnding);
            SystemEvents.SessionSwitch += new SessionSwitchEventHandler(SystemEvents_SessionSwitch);
        }

        /// <summary>
        /// Detect user logging off and play the appropriate logoff/shutdown sound
        /// </summary>
        private void SystemEvents_SessionEnding(object sender, SessionEndingEventArgs e)
        {
            this.Text = RuntimeConfig.AppDisplayName;

            if ((e.Reason == SessionEndReasons.SystemShutdown || Settings.PreferStartupSoundOnLogon) && File.Exists(soundShutdown.FilePath))
            {
                ShutdownBlockReasonCreate(this.Handle, Translations.Get("playing_shutdown_sound"));
                if (e.Reason == SessionEndReasons.SystemShutdown)
                    File.Delete(LastBootFile); // Force Startup sound next time
                PlaySound(soundShutdown);
            }
            else
            {
                ShutdownBlockReasonCreate(this.Handle, Translations.Get("playing_logoff_sound"));
                PlaySound(soundLogoff);
            }

            ShutdownBlockReasonDestroy(this.Handle);
        }

        /// <summary>
        /// Detect user leaving and resuming session
        /// </summary>
        private void SystemEvents_SessionSwitch(object sender, SessionSwitchEventArgs e)
        {
            if (e.Reason == SessionSwitchReason.SessionLock && File.Exists(soundLogoff.FilePath))
            {
                PlaySound(soundLogoff);
            }
            else if (e.Reason == SessionSwitchReason.SessionUnlock && File.Exists(soundLogon.FilePath))
            {
                PlaySound(soundLogon);
            }
        }

        /// <summary>
        /// Detect when the background sound player window is focused
        /// </summary>
        private void WindowFocused(object sender, EventArgs e)
        {
            // Refuse focus - Some screen readers may pick up the window (issue #8)
            try
            {
                // Switch focus to the Windows Desktop's folderView
                IntPtr desktop = IntPtr.Zero;
                if (WindowManager.GetDesktopWindow(ref desktop))
                    WindowManager.SetForegroundWindow(desktop);
            }
            catch
            {
                // Avoid crashes linked to this workaround
            }
        }

        /// <summary>
        /// Make sure the window stays outside the screen.
        /// Screen layout change may place the window back into visible area.
        /// </summary>
        private void OnLocationChanged(object sender, EventArgs e)
        {
            this.LocationChanged -= new EventHandler(OnLocationChanged);
            this.Location = new System.Drawing.Point(-9999, -9999);
            this.LocationChanged += new EventHandler(OnLocationChanged);
        }

        /// <summary>
        /// On some conditions, the system may automatically minimize the window.
        /// Cannot stay minimized because it may show a window title box next to the task bar.
        /// </summary>
        private void OnResize(object sender, EventArgs e)
        {
            this.Resize -= new EventHandler(OnResize);
            if (WindowState == FormWindowState.Minimized)
                this.WindowState = FormWindowState.Normal;
            this.Resize += new EventHandler(OnResize);
        }

        /// <summary>
        /// Override "Show without activation" property to not focus the window on launch
        /// </summary>
        protected override bool ShowWithoutActivation
        {
            get
            {
                return true;
            }
        }
    }
}
