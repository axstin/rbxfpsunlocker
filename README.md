## Usage
1. Download the latest release from https://github.com/axstin/rbxfpsunlocker/releases
2. Extract `rbxfpsunlocker.zip` into a folder
3. Run `injector.exe` before or after joining a Roblox game
4. Enjoy those [beautiful frames](https://i.imgur.com/vsLf04O.png) 👌

## FAQ

1. **Some files are being deleted and/or my anti-virus is detecting rbxfpsunlocker as malware. What do I do?**
Any detections are most likely a false positive. If you don't trust me, feel free to download the repository and compile the project from source. Otherwise, add an exception to your antivirus for `injector.exe` and `rbxfpsunlocker.dll` (or the folder they are both in).

2. **How do I fix "X dll is missing from your computer" or "The application was unable to start correctly" errors?**
Download and install [vc_redist.x86.exe](https://i.imgur.com/dDB1ifs.png) from Microsoft's website: https://www.microsoft.com/en-us/download/details.aspx?id=48145

3. **I built the project, and whenever I try to inject it, the injector says success and closes, but it doesn't work.** There is a fair chance that you built the DLL for a 64 bit target. Since ROBLOX is built for 32 bit, you cannot inject a 64 bit DLL into it. Make sure that "x86" is selected at the top of Visual Studio next to the Debug / Release dropdown.

## Contact
Discord: `Austin#7878 (ID: 72425749818126336)`
