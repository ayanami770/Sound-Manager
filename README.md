![SoundManager](Images/logo-en.png)

SoundManager is free software that makes it easy to create and share Windows sound schemes. This fork supports 64-bit Windows 7 SP1 through Windows 11. Requires [.NET 4.0](http://www.microsoft.com/en-us/download/details.aspx?id=17718) or greater; on Windows 7/8.1, the [Universal C Runtime update](https://support.microsoft.com/kb/2999226) is also required.

* 💾 **Download:** Have a look at the [releases section](https://github.com/ORelio/Sound-Manager/releases) to get a build
* 📁 **Sound schemes:** Check out the [sound schemes repository](https://github.com/ORelio/Sound-Manager-Schemes)

## About this fork

This is a fork of [ORelio/Sound-Manager](https://github.com/ORelio/Sound-Manager) with the following changes:

* 64-bit (x64) build — support for 32-bit systems and Windows XP is dropped
* The bundled third-party libraries (Ionic.Zip, NAudio) are replaced by SmNative,
  a self-contained C99 library housed in [`SmNative/`](SmNative/): zip archive
  handling with its own DEFLATE implementation, and audio conversion through
  Windows Media Foundation
* The WinForms GUI is replaced by a `make menuconfig`-style terminal UI
  (powered by [Universal-TUI](https://github.com/ayanami770/Universal-TUI),
  referenced as a git submodule) plus CLI subcommands; the separate
  DownloadSchemes.exe became the `download` command
* Building no longer requires Visual Studio or the Windows SDK
  (see [Build instructions](#build-instructions))

## Overview

Run `SoundManager.exe` with no arguments to open the menuconfig-style
configuration UI: sound events (enable/disable and assigned files), scheme
metadata, and all settings, edited from the terminal with full keyboard, mouse
and UTF-8 support. Everything else is a CLI subcommand — run
`SoundManager help` for the list (`set`, `play`, `import`, `export`,
`download`, ...).

Main features are the following:

* Play missing sounds on Windows 8 and greater
* Export and import sound schemes using archive files
* Load and test sound files for each event
* Define metadata such as thumbnail, author, description
* Auto-convert sounds to WAV format (Windows 7+)
* Patch built-in startup sound (Admin required, Windows Vista+)
* Import sound schemes created with the [Sound applet](https://www.thewindowsclub.com/change-sounds-in-windows)
* Import proprietary "soundpack" archive files
* Export sound schemes as installable "themepack" files (Windows 7+)

## User Manual

See [User Manual](UserManual/Readme-En.txt) for more details on how to use the program.

## How it works

### The `SoundManager` sound scheme

SoundManager integrates into the system using the built-in sound scheme feature in Registry:
````
HKEY_CURRENT_USER\AppEvents\Schemes
````
The `SoundManager` scheme is automatically created on first launch, pointing to:
````
C:\Users\%USERNAME%\AppData\Roaming\SoundManager\Media\
````
Sound files such as `Startup.wav`, `Shutdown.wav` and so on are placed here. Since they are automatically played by the system, the SoundManager app is not required to run once the sound scheme has been set, except if you want to restore the missing sounds on Windows 8+ (see below).

SoundManager handles registry differences between Windows versions, such as the balloon sound which [does not play by default](https://winaero.com/blog/fix-windows-plays-no-sound-for-tray-balloon-tips-notifications/) on Windows 7/8 and changes again on Windows 10.

### Sound Archives

Sound archive files are simply Zip files having the `.ths` file extension:

````
SoundScheme.ths
 |- Scheme.ini
 |- Scheme.png
 |- Startup.wav
 |- Shutdown.wav
 \- <OtherSounds>.wav
````

SoundManager can associate itself with this file type to conveniently load sound schemes, and you can manually edit them using any file archive utility such as [7-Zip](https://www.7-zip.org/) or by renaming them to `.zip` while displaying [file extensions](https://www.thewindowsclub.com/show-file-extensions-in-windows), then using the built-in Windows utility.

### Windows Vista+ startup sound

On Windows Vista and greater, the startup sound is no longer customizable by the user, the corresponding WAV file being embedded in `C:\Windows\System32\imageres.dll` for [performance reasons](https://blogs.msdn.microsoft.com/e7/2009/02/18/engineering-the-windows-7-boot-animation/).

SoundManager can optionally [patch imageres.dll](https://www.sevenforums.com/tutorials/63398-startup-sound-change-windows-7-a.html) to update the startup sound:

* Ownership of `imageres.dll` is [transferred](https://helpdeskgeek.com/windows-7/windows-7-how-to-delete-files-protected-by-trustedinstaller/) from `TrustedInstaller` to `Administrators`
* If not already done, `imageres.dll` is backed up to `imageres.dll.bak`
* Existing `imageres.dll` is moved to `imageres.dll.old` since it is in use by the system
* `imageres.dll.bak` is copied to `imageres.dll` and its `WAV` resourse is updated

This feature requires administrator privileges. If enabled, SoundManager will show an [UAC](https://en.wikipedia.org/wiki/User_Account_Control) prompt on launch. Due to `imageres.dll` files being used by the system, SoundManager might not be able to patch the startup sound more than once between each system reboot. Also, major system updates might revert the startup sound to its original state and/or break the patch mechanism.

Initially implemented using [Resource Hacker](https://www.angusj.com/resourcehacker/), SoundManager now patches the DLL directly using the Windows API ([BeginUpdateResource](http://msdn.microsoft.com/en-us/library/windows/desktop/ms648030%28v=vs.85%29.aspx), [UpdateResource](http://msdn.microsoft.com/en-us/library/windows/desktop/ms648049%28v=vs.85%29.aspx), [EndUpdateResource](http://msdn.microsoft.com/en-us/library/windows/desktop/ms648032%28v=vs.85%29.aspx)) to replace the startup sound. This allows seamless patching on newer system versions that implement a [distinct resource file for the DLL](https://learn.microsoft.com/en-us/answers/questions/4067434/how-would-one-change-the-boot-up-sound-on-windows?page=1#answers) `imageres.dll.mun`.

### Windows 8+ shutdown, login, logoff sounds

On Windows 8, the shutdown sound was removed for further [performance reasons](https://winaero.com/blog/how-to-play-the-logon-or-startup-sound-in-windows-8-1-or-windows-8/), as well as the logon and logoff sounds. SoundManager can emulate the playback of these sounds by launching a background process on logon:

* Process spawns an invisible window, mandatory for delaying system shutdown
* Process plays Startup or Logon sound and goes inactive
* On logoff, process wakes up and [sets up a ShutdownBlockReason](https://devblogs.microsoft.com/oldnewthing/20120614-00/?p=7373)
* Process determines if the Logoff or Shutdown sound should be played
* Sound is played, then ShutdownBlockReason is removed and the process exits

This is typically how `explorer.exe` was handling the thing on Windows 7, but you'll get yet another process sleeping in background, separate from `explorer.exe`. This feature can be disabled entierely in the SoundManager settings.

SoundManager also allows patching the startup sound on Windows 8 and greater. When used in combination with the background sound player process, the system itself will play the native startup sound, and the background process from SoundManager will play the other sounds. This helps reducing latency in startup sound playback since the system will play the startup sound with high priority.

## Build instructions

This fork targets .NET Framework v4.0 (x64) and builds with the MSBuild bundled inside the .NET Framework itself — no Visual Studio or Windows SDK required. Importing the solution into a recent [Visual Studio](https://visualstudio.microsoft.com/vs/community/) also works.

### Getting the source

Clone with submodules — the TUI engine lives in the
[Universal-TUI](https://github.com/ayanami770/Universal-TUI) submodule at
`SmNative\utui`:

````
git clone --recurse-submodules https://github.com/ayanami770/Sound-Manager.git
````

For an existing clone, run `git submodule update --init` instead.

### Compiling the C# solution

From the project folder (where `README.md` and `SoundManager.sln` are housed):

````
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe SoundManager.sln /p:Configuration=Release /p:Platform=x64
````

On machines without the .NET reference assemblies installed (no Visual Studio), append:

````
"/p:FrameworkPathOverride=C:\Windows\Microsoft.NET\Framework64\v4.0.30319"
````

If everything worked properly, you should get:

* `SoundManager.exe` and `SmNative.dll` in `<ProjectFolder>\SoundManager\bin\Release`
* `SoundManagerBg.exe` in `<ProjectFolder>\SoundManagerBg\bin\Release`

Then copy the following items into `<ProjectFolder>\SoundManager\bin\Release`:

* `<ProjectFolder>\SoundManager\Lang`
* `<ProjectFolder>\UserManual\Readme-En.txt`
* `<ProjectFolder>\UserManual\Readme-Fr.txt`
* `<ProjectFolder>\SoundManagerBg\bin\Release\SoundManagerBg.exe`

Finally, check that everything's working by launching `<ProjectFolder>\SoundManager\bin\Release\SoundManager.exe` from a terminal.

### Rebuilding SmNative.dll

The native library `SoundManager\SmNative.dll` is committed to the repository, so this step is only needed after changing the C sources in `SmNative\` or updating the Universal-TUI submodule. Install [MSYS2](https://www.msys2.org/) with the CLANG64 toolchain (`pacman -S mingw-w64-clang-x86_64-clang`), make sure the submodule is initialized, then run `SmNative\build.cmd`. This builds `SmNative.dll` and the `test.exe` test harness, and copies the DLL into the SoundManager project. Run `test.exe selftest <tmpdir>` to check the library.

## License

SoundManager is provided under
[CDDL-1.0](http://opensource.org/licenses/CDDL-1.0)
([Why?](http://qstuff.blogspot.fr/2007/04/why-cddl.html)).

Basically, you can use it or its source for any project, free or commercial, but if you improve it or fix issues,
the license requires you to contribute back by submitting a pull request with your improved version of the code.
Also, credit must be given to the original project, and license notices may not be removed from the code.
