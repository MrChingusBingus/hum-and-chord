# Hum and Chord

A songwriting helper for people who don't know theory. Play or sing an idea, or let it
listen to your DAW, and it tells you the key and suggests chords that fit.

This project builds it as:

| Build | For | How it works |
| --- | --- | --- |
| Audio Unit **MIDI effect** | Logic | Goes in the track's MIDI FX slot. Hears the region and your playing, and sends chords to the track's instrument. |
| Audio Unit **audio effect** ("Hum and Chord Audio") | Logic audio tracks | Listens to the track's audio, recorded or live, like an EQ would. |
| **VST3** | Reaper | Goes on any track. Reads MIDI and audio; on MIDI tracks it goes before the instrument. |
| **App** | Anywhere | The same thing as a standalone Mac app, with the mic for singing. |

## Getting the plugins

You don't need to build anything on your Mac. Every push to `main` runs
**Build Mac plugins** on GitHub (about 15 minutes). When it's green:

1. Open the repository's **Releases** page and download `Hum-and-Chord-mac.zip`.
2. Unzip it and follow `READ-ME-FIRST.txt`.

To build again without changing anything: **Actions → Build Mac plugins → Run workflow**.

## How it's put together

- `web/index.html` is the whole interface. It's the same page as the browser version, so
  you can open it in Chrome to try changes before building.
- `Source/PluginProcessor.cpp` passes your MIDI through untouched, copies each note (with its
  beat position from the DAW) for the interface, and adds any chords the interface asks
  to play.
- `Source/AudioAnalyser.cpp` reads audio: it tracks pitch for single-note parts (YIN) and
  measures how much of each of the 12 notes is sounding for chords and mixes. The audio thread
  only makes a light 12 kHz copy; the analysis runs elsewhere, so it adds no latency.
- Listening only happens while the plugin window is open. Audio and MIDI always pass
  through untouched.
- `Source/PluginEditor.cpp` shows the page in a web view and connects the two:
  track notes and transport go to the page; chords, saved songs, and files come back.
- Songs are saved inside your DAW project.

## Building in Xcode on your own Mac (optional)

Needs Xcode and CMake (`brew install cmake`). From the project folder:

```
cmake -S . -B build-xcode -G Xcode -DHC_COPY_PLUGINS=ON
open build-xcode/HumAndChord.xcodeproj
```

Pick a scheme (for example `HumAndChordMidiFX_AU`) and press Build. With `HC_COPY_PLUGINS=ON`,
each plugin is installed into `~/Library/Audio/Plug-Ins` after it builds.

## Making an installer

```
bash packaging/make-installer.sh build-xcode   # uses your Xcode Release build
bash packaging/make-installer.sh               # or: builds everything for Apple Silicon + Intel first
```

This makes `dist-installer/Hum-and-Chord-<version>.pkg`, a normal Mac installer that puts both
Logic plugins, the VST3, and the app in the system folders. GitHub builds the same installer on
every push and puts it on the Releases page.

To share it with other people without macOS warnings, you need a paid Apple Developer account.
Set `DEVELOPER_ID_APPLICATION`, `DEVELOPER_ID_INSTALLER` and `NOTARY_PROFILE` before running the
script (see the top of the script).

Built with [JUCE](https://juce.com) 8. JUCE is free for personal use; check its license
before selling anything built with it.
