#!/bin/bash
# Makes the installer disk image: the app beside a link to /Applications, to
# drag it onto. Usage: make-mac-dmg.sh <App.app> <out.dmg>
set -euo pipefail

app="$1"
dmg="$2"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

ditto --norsrc --noextattr --noqtn "$app" "$stage/$(basename "$app")"
xattr -cr "$stage"
ln -s /Applications "$stage/Applications"
rm -f "$dmg"
hdiutil create -volname "OpenA2Racer" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$dmg" >/dev/null
echo "-- Disk image: $dmg"
