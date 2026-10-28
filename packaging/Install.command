#!/bin/bash
# Installs Hum and Chord for Logic (Audio Unit) and Reaper (VST3), plus the app.
cd "$(dirname "$0")" || exit 1

COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$COMPONENTS" "$VST3"

# Files from the internet are quarantined by macOS. These were built by your own
# GitHub project, so clear that flag so Logic and Reaper can load them.
xattr -dr com.apple.quarantine . 2>/dev/null

rm -rf "$COMPONENTS/Hum and Chord.component" "$VST3/Hum and Chord.vst3"
cp -R "Hum and Chord.component" "$COMPONENTS/" && echo "Installed the Audio Unit for Logic."
cp -R "Hum and Chord.vst3" "$VST3/" && echo "Installed the VST3 for Reaper."

if [ -d "Hum and Chord.app" ]; then
  rm -rf "/Applications/Hum and Chord.app"
  if cp -R "Hum and Chord.app" /Applications/ 2>/dev/null; then
    echo "Installed the app in Applications."
  else
    mkdir -p "$HOME/Applications"
    rm -rf "$HOME/Applications/Hum and Chord.app"
    cp -R "Hum and Chord.app" "$HOME/Applications/" && echo "Installed the app in your Applications folder."
  fi
fi

# Make macOS notice the new Audio Unit without a restart.
killall -9 AudioComponentRegistrar 2>/dev/null

echo
echo "Done. Quit and reopen Logic and Reaper, then look for \"Hum and Chord\"."
echo "You can close this window."
