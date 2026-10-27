ToneShaper - adaptive tone shaper (AU / VST3 / Standalone) for macOS
====================================================================

Universal build: runs natively on Apple Silicon and Intel Macs, macOS 10.13 or later.

INSTALL (easiest): run the .pkg installer from the release page.
  It installs system-wide:
    /Library/Audio/Plug-Ins/Components/ToneShaper.component   (Audio Unit)
    /Library/Audio/Plug-Ins/VST3/ToneShaper.vst3              (VST3)
    /Applications/ToneShaper.app                              (Standalone)

INSTALL (this zip, no admin password): double-click "Install ToneShaper.command".
  It copies everything into your user Library and ~/Applications and clears the
  download quarantine flag.

"Unidentified developer" / "cannot be opened" warnings
  These builds are ad-hoc signed, not notarised by Apple. The first time you open
  the .pkg or the .command file:
    - right-click it and choose Open, or
    - open System Settings > Privacy & Security, scroll down and click "Open Anyway".
  If a DAW still refuses to load the plug-in after a manual copy, run in Terminal:
    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/ToneShaper.component
    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ToneShaper.vst3

Then restart the DAW or rescan plug-ins. In Logic Pro it appears under
Audio Units > Arent Todal > ToneShaper.

UNINSTALL: delete the three items listed above (or the ones in ~/Library and
~/Applications if you used the .command installer).

User presets are stored in ~/Library/Audio/Presets/Arent Todal/ToneShaper/.
