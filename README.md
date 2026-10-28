# Hum and Chord

A songwriting helper for people who don't know theory. Play or sing an idea, or let it
listen to your DAW, and it tells you the key and suggests chords that fit.

This project builds it as:

| Build | For | How it works |
| --- | --- | --- |
| Audio Unit **MIDI effect** | Logic | Goes in the track's MIDI FX slot. Hears the region and your playing, and sends chords to the track's instrument. |
| **VST3** | Reaper | Goes in the track's FX chain before the instrument. MIDI in, MIDI out. |
| **App** | Anywhere | The same thing as a standalone Mac app. |

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
- `Source/PluginEditor.cpp` shows the page in a web view and connects the two:
  track notes and transport go to the page; chords, saved songs, and files come back.
- Songs are saved inside your DAW project.

## Building on your own Mac (optional)

Needs Xcode and CMake (`brew install cmake`):

```
cmake -S . -B build -G Xcode
cmake --build build --config Release
```

The plugins end up in `build/*_artefacts/Release/`.

Built with [JUCE](https://juce.com) 8. JUCE is free for personal use; check its license
before selling anything built with it.
