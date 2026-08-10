using System;
using System.Windows.Forms;

namespace SoundManager
{
    /// <summary>
    /// Entry point of the background sound player (SoundManagerBg.exe).
    /// A separate windowless executable so that the main SoundManager.exe can be
    /// a plain console application while this one keeps the hidden window
    /// required by the ShutdownBlockReason API.
    /// By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
    /// </summary>
    static class ProgramBg
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new BgSoundPlayer());
        }
    }
}
