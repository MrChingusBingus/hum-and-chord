#!/bin/bash
# Builds a standard macOS installer: dist-installer/Hum-and-Chord-<version>.pkg
#
#   bash packaging/make-installer.sh              build everything (Apple Silicon + Intel), then package
#   bash packaging/make-installer.sh build-xcode  package what you already built in Xcode (Release)
#
# Optional, for sharing with other people (needs a paid Apple Developer account):
#   DEVELOPER_ID_APPLICATION="Developer ID Application: Your Name (TEAMID)"
#   DEVELOPER_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)"
#   NOTARY_PROFILE="hum-and-chord"   (a profile saved with: xcrun notarytool store-credentials)
set -euo pipefail

cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project(HumAndChord VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
VERSION=${VERSION:-1.0.0}
BUILD=${1:-}

if [ -z "$BUILD" ]; then
  BUILD=build-release
  echo "== Building Hum and Chord $VERSION for Apple Silicon and Intel (this takes a while)"
  cmake -S . -B "$BUILD" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
  cmake --build "$BUILD" --config Release --parallel
fi

MIDIFX="$BUILD/HumAndChordMidiFX_artefacts/Release/AU/Hum and Chord.component"
AUDIOFX="$BUILD/HumAndChordAudioFX_artefacts/Release/AU/Hum and Chord Audio.component"
VST3="$BUILD/HumAndChord_artefacts/Release/VST3/Hum and Chord.vst3"
APP="$BUILD/HumAndChord_artefacts/Release/Standalone/Hum and Chord.app"

missing=0
for item in "$MIDIFX" "$AUDIOFX" "$VST3" "$APP"; do
  if [ ! -d "$item" ]; then
    echo "Missing: $item"
    missing=1
  fi
done
if [ "$missing" = 1 ]; then
  echo
  echo "Build all four in Release first. In Terminal:"
  echo "  cmake --build $BUILD --config Release"
  exit 1
fi

OUT=dist-installer
WORK="$OUT/work"
rm -rf "$OUT"
mkdir -p "$WORK/resources" "$WORK/scripts"

sign_bundle() {
  local bundle=$1 entitlements=${2:-}
  if [ -n "${DEVELOPER_ID_APPLICATION:-}" ]; then
    if [ -n "$entitlements" ]; then
      codesign --force --deep --timestamp --options runtime --entitlements "$entitlements" \
               --sign "$DEVELOPER_ID_APPLICATION" "$bundle"
    else
      codesign --force --deep --timestamp --options runtime --sign "$DEVELOPER_ID_APPLICATION" "$bundle"
    fi
  else
    codesign --force --deep --sign - "$bundle"
  fi
}

# make_component <name> <bundle> <install folder> [entitlements]
make_component() {
  local name=$1 bundle=$2 dest=$3 entitlements=${4:-}
  local stage="$WORK/stage-$name" plist="$WORK/$name.plist"
  mkdir -p "$stage"
  ditto "$bundle" "$stage/$(basename "$bundle")"
  sign_bundle "$stage/$(basename "$bundle")" "$entitlements"

  # Always install to the folder below. Without this, Installer may "relocate" the
  # update onto another copy it finds elsewhere on the disk.
  pkgbuild --analyze --root "$stage" "$plist" >/dev/null
  local count i
  count=$(/usr/libexec/PlistBuddy -c "Print" "$plist" | grep -c "BundleIsRelocatable" || true)
  i=0
  while [ "$i" -lt "$count" ]; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
    i=$((i + 1))
  done

  pkgbuild --root "$stage" --component-plist "$plist" \
           --identifier "com.humandchord.pkg.$name" --version "$VERSION" \
           --install-location "$dest" "$WORK/$name.pkg"
}

echo "== Packaging"
cat > "$WORK/app.entitlements" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.device.audio-input</key>
    <true/>
</dict>
</plist>
EOF

make_component midifx  "$MIDIFX"  "/Library/Audio/Plug-Ins/Components"
make_component audiofx "$AUDIOFX" "/Library/Audio/Plug-Ins/Components"
make_component vst3    "$VST3"    "/Library/Audio/Plug-Ins/VST3"
make_component app     "$APP"     "/Applications" "$WORK/app.entitlements"

# Runs after installing: removes older copies from your user plug-in folders (from
# Install.command or Xcode builds) so Logic and Reaper don't list Hum and Chord twice,
# and makes macOS rescan Audio Units.
cat > "$WORK/scripts/postinstall" <<'EOF'
#!/bin/bash
console_user=$(stat -f%Su /dev/console 2>/dev/null || true)
if [ -n "$console_user" ] && [ "$console_user" != "root" ]; then
  home=$(dscl . -read "/Users/$console_user" NFSHomeDirectory 2>/dev/null | awk '{print $2}')
  if [ -n "$home" ] && [ -d "$home/Library/Audio/Plug-Ins" ]; then
    rm -rf "$home/Library/Audio/Plug-Ins/Components/Hum and Chord.component" \
           "$home/Library/Audio/Plug-Ins/Components/Hum and Chord Audio.component" \
           "$home/Library/Audio/Plug-Ins/VST3/Hum and Chord.vst3"
  fi
fi
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x "$WORK/scripts/postinstall"
pkgbuild --nopayload --scripts "$WORK/scripts" \
         --identifier com.humandchord.pkg.tidyup --version "$VERSION" "$WORK/tidyup.pkg"

cat > "$WORK/resources/welcome.html" <<EOF
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>Hum and Chord $VERSION</h2>
<p>Play, sing, or let it listen to your DAW, and it suggests chords that fit.</p>
<p>This installs:</p>
<ul>
<li><b>Hum and Chord</b> for Logic's MIDI FX slot (software instrument tracks)</li>
<li><b>Hum and Chord Audio</b> for Logic's Audio FX slots (audio tracks)</li>
<li><b>Hum and Chord VST3</b> for Reaper</li>
<li>The <b>Hum and Chord</b> app</li>
</ul>
<p>Click <b>Customize</b> on the next screen to leave any of them out.</p>
</body></html>
EOF

cat > "$WORK/resources/conclusion.html" <<'EOF'
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>All set</h2>
<p>Quit and reopen Logic and Reaper.</p>
<p><b>Logic:</b> on an instrument track, click the MIDI FX slot and choose Audio Units &gt; Hum and Chord.
On an audio track, use an Audio FX slot and choose Hum and Chord Audio.
If it doesn't appear, open Logic Pro &gt; Settings &gt; Plug-in Manager and click Reset &amp; Rescan Selection.</p>
<p><b>Reaper:</b> Preferences &gt; Plug-ins &gt; VST &gt; Re-scan, then add "VST3: Hum and Chord" to a track.</p>
</body></html>
EOF

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Hum and Chord</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <choices-outline>
        <line choice="midifx"/>
        <line choice="audiofx"/>
        <line choice="vst3"/>
        <line choice="app"/>
        <line choice="tidyup"/>
    </choices-outline>
    <choice id="midifx" title="Logic: Hum and Chord (MIDI FX)"
            description="For software instrument tracks. Goes in the MIDI FX slot.">
        <pkg-ref id="com.humandchord.pkg.midifx"/>
    </choice>
    <choice id="audiofx" title="Logic: Hum and Chord Audio"
            description="For audio tracks. Goes in an Audio FX slot and listens to the audio.">
        <pkg-ref id="com.humandchord.pkg.audiofx"/>
    </choice>
    <choice id="vst3" title="Reaper: Hum and Chord VST3"
            description="For Reaper and other VST3 hosts. Works on audio and MIDI tracks.">
        <pkg-ref id="com.humandchord.pkg.vst3"/>
    </choice>
    <choice id="app" title="Hum and Chord app"
            description="The standalone app, with the microphone for singing.">
        <pkg-ref id="com.humandchord.pkg.app"/>
    </choice>
    <choice id="tidyup" visible="false" title="Tidy up">
        <pkg-ref id="com.humandchord.pkg.tidyup"/>
    </choice>
    <pkg-ref id="com.humandchord.pkg.midifx" version="$VERSION">midifx.pkg</pkg-ref>
    <pkg-ref id="com.humandchord.pkg.audiofx" version="$VERSION">audiofx.pkg</pkg-ref>
    <pkg-ref id="com.humandchord.pkg.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="com.humandchord.pkg.app" version="$VERSION">app.pkg</pkg-ref>
    <pkg-ref id="com.humandchord.pkg.tidyup" version="$VERSION">tidyup.pkg</pkg-ref>
</installer-gui-script>
EOF

PKG="$OUT/Hum-and-Chord-$VERSION.pkg"
if [ -n "${DEVELOPER_ID_INSTALLER:-}" ]; then
  productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" \
               --resources "$WORK/resources" --sign "$DEVELOPER_ID_INSTALLER" "$PKG"
else
  productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" \
               --resources "$WORK/resources" "$PKG"
fi

if [ -n "${NOTARY_PROFILE:-}" ]; then
  echo "== Sending to Apple for notarization"
  xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$PKG"
fi

rm -rf "$WORK"
echo
echo "Installer ready: $PKG"
if [ -z "${DEVELOPER_ID_INSTALLER:-}" ]; then
  echo "(Not signed with a Developer ID. Fine on this Mac; on other Macs, macOS will ask"
  echo " the person to allow it in System Settings > Privacy & Security.)"
fi
