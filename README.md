## Usage
1. Download the latest release from https://github.com/axstin/rbxfpsunlocker/releases
2. Extract `rbxfpsunlocker-x64.zip` or `rbxfpsunlocker-x86.zip` into a folder
3. Run `rbxfpsunlocker.exe` before or after starting Roblox
4. Enjoy those [beautiful frames](https://i.imgur.com/vsLf04O.png) 👌

## Submitting an Issue

**NOTICE: Issues are currently closed due to spam and number of non-issues or low quality submissions. They will be reopened once I have the time and capacity to moderate them. Sorry!**

Before [submitting an issue](https://github.com/axstin/rbxfpsunlocker/issues/new), please:
- Make sure you submit an issue or suggestion
- Make sure your question or problem cannot be answered in the FAQ below
- Please read previous issues to make sure your question was not asked before
- Be as informative as possible: issues with ambiguous titles and missing/low-quality descriptions will be deleted
- Include a copy of RFU's console (tray icon->Toggle Console). You can censor any personal information (file paths and so on)
- Include steps on how to reproduce the issue, if possible
- Include on what platform(s) the issue occurs (normal client, Windows 10 Roblox app, Roblox Studio)
- If relevant, include system specifications (e.g. GPU model) and monitor refresh rates

Issues submitted not attempting to follow these guidelines will be closed or deleted.

## FAQ

1. **Roblox is force-closing, files are being deleted, and/or my anti-virus is detecting rbxfpsunlocker as malware. What do I do?**

All detections are false positives. Internally, RFU "tampers" with running Roblox processes in order to uncap framerate and can appear as suspicious to an anti-virus. For reasons unbeknownst to me, 32-bit builds of RFU garner many more false positive detections than 64-bit builds and are no longer included in new releases. If you don't trust me, feel free to download the repository, review the source code, and compile the project yourself with Visual Studio 2019. Otherwise, add an exception to your anti-virus for `rbxfpsunlocker.exe` (or the folder it is in).

2. **How can I see my FPS?**

Press `Shift+F5` in-game to view your FPS. In Roblox Studio, go to View->Stats->Summary.

3. **How do I resolve choppiness and input lag at high framerates?**

Try entering fullscreen using `Alt+Enter`.

4. **I used this unlocker and my framerate is the same or below 60. Why?**

I say with great emphasis, as this seems to be a common misconception, that Roblox FPS Unlocker is an FPS _unlocker_ and not a _booster_. It will not boost Roblox's performance in any way and only removes Roblox's 60 FPS limit. To take advantage of RFU, a computer powerful enough to run Roblox at more than 60 FPS is required.

This being said, if you know your computer is powerful enough but still aren't seeing higher framerates with the unlocker, feel free to [submit an issue](https://github.com/axstin/rbxfpsunlocker/#submitting-an-issue).

5. **Can I set a custom framerate cap?**

Custom framerate limits can be set by changing the `FPSCap` value inside the `settings` file located in the same folder as `rbxfpsunlocker.exe` and reloading settings (RFU->Load Settings). Changing the cap with RFU's menu will reset/overwrite this value.

6. **Does this work for Mac?**

No. Roblox FPS Unlocker was written only for the Windows platform and I [currently have no plans to change this](https://github.com/axstin/rbxfpsunlocker/issues/49). However, those more experienced and with Mac hardware are free to port this project to Mac!

7. **Why do I get a "Failed to connect to Github" error?**

This error means Roblox FPS Unlocker could not connect to the Internet to check for updates. This may be due to your anti-virus, computer firewall, network firewall, or etc. blocking the request. The error can be safely ignored by pressing "Ok".

8. **Why do I get a "Variable scan failed" error?**

This means RFU was unable to find the internal variable responsible for uncapping Roblox's framerate. This might happen if another program has already edited the value (e.g. an exploit). Please verify that your framerate is at a stable ~60.0 FPS (Shift+F5) before using the unlocker. If it is and the error still occurs, please [submit an issue](https://github.com/axstin/rbxfpsunlocker/#submitting-an-issue).

9. **How do I uninstall Roblox FPS Unlocker?**

RFU does not install itself anywhere. It can be deleted by simply exiting the program if it is open (tray icon->Exit) and deleting `rbxfpsunlocker.exe`.

##  Disclaimer

Around June 21st, 2018 I received reports that Roblox was [handing out bans](https://i.imgur.com/i4NEGB0.png) to players using this tool. Roblox most likely assumes that `rbxfpsunlocker.dll` is an exploit or a cheat. I fixed this issue in [release 1.6](https://github.com/axstin/rbxfpsunlocker/releases/tag/v1.6) and can confirm injecting rbxfpsunlocker is now "invisible" to Roblox. **HOWEVER, this won't stop Roblox from releasing a new detection in the future. Please use this tool at your own risk** and keep in touch with this repository for updates.

<sub>roblox this isn't an exploit no bans please :(</sub>

**EDIT:** [Version 4.0](https://github.com/axstin/rbxfpsunlocker/releases/tag/v4.0) of Roblox FPS Unlocker further reduces the risk of bans or warns (one could argue the chances are now 0) as DLL injection is no longer used. See the changelog for more information.

**EDIT (August 11, 2019):** At the engineering panel on day 1 of RDC 2019, Adam Miller, VP of Engineering & Technology at Roblox, made a personal guarantee that anyone using Roblox FPS Unlocker will not be banned from Roblox. This was in response to the question "Why does Roblox have a FPS cap and why is it against the rules to change that cap?" to which [Arseny Kapoulkine (zeuxcg)](https://twitter.com/zeuxcg) also revealed that Roblox will be adding support for "higher refresh rate monitors" potentially by next year. [See the video here!](https://youtu.be/5gNzFsJlFbo?t=143)


