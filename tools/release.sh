#!/bin/bash
# Uploads a GitHub release with firmware.bin, spiffs.bin and bootloader.zip.
#
# Usage (from the repository root, after building and copying the files to builds/vX.Y.Z/):
#   tools/release.sh 0.11.0
#   tools/release.sh 0.11.0 --update   replaces files and notes of an existing release and moves its tag to origin/main
#
# The release notes are taken from the changelog section of this version in README.md.
# The commit that is pushed to origin/main is used as release commit, so push first.
set -e
VERSION=${1#v}
UPDATE=0
[ "$2" = "--update" ] && UPDATE=1
REPO=Kalored/FingerprintDoorbell-NG
TAG=v$VERSION
DIR=builds/$TAG

[ -n "$VERSION" ] || { echo "usage: $0 <version> [--update], e.g. $0 0.11.0"; exit 1; }
cd "$(dirname "$0")/.."
command -v gh >/dev/null || { echo "GitHub CLI (gh) is missing"; exit 1; }

for f in "$DIR/firmware.bin" "$DIR/spiffs.bin" doc/bootloader.zip; do
  [ -f "$f" ] || { echo "missing: $f"; exit 1; }
done
# the firmware must contain the version string, so an old file is not uploaded by mistake
grep -qa "$VERSION" "$DIR/firmware.bin" || { echo "$DIR/firmware.bin does not contain version $VERSION"; exit 1; }

EXISTS=0
gh release view "$TAG" -R "$REPO" >/dev/null 2>&1 && EXISTS=1
if [ $EXISTS = 1 ] && [ $UPDATE = 0 ]; then
  echo "release $TAG already exists: https://github.com/$REPO/releases/tag/$TAG"
  echo "use \"$0 $VERSION --update\" to replace its files and notes"
  exit 1
fi
if [ $EXISTS = 0 ] && [ $UPDATE = 1 ]; then
  echo "release $TAG does not exist yet, run without --update"
  exit 1
fi

git fetch -q origin main
if [ -n "$(git status --porcelain --untracked-files=no)" ] || [ "$(git rev-parse HEAD)" != "$(git rev-parse origin/main)" ]; then
  echo "warning: local changes or commits that are not pushed, the release uses origin/main"
fi
COMMIT=$(git rev-parse origin/main)

# release notes: update hint + the changelog entry of this version from README.md
NOTES=$(mktemp)
trap 'rm -f "$NOTES"' EXIT
python3 - "$VERSION" > "$NOTES" <<'PY'
import re, sys
version = re.escape(sys.argv[1])
readme = open('README.md', encoding='utf-8').read()
m = re.search(r'^### ' + version + r' · (.*?)(?: \(latest\))?\n(.*?)(?=\n#|\n<details>|\Z)', readme, re.S | re.M)
if not m:
    m = re.search(r'<summary><b>' + version + r'</b> · (.*?)</summary>\n\n(.*?)\n\n</details>', readme, re.S)
if not m:
    sys.exit('no changelog entry for this version in README.md')
print('## ' + m.group(1).strip() + '\n')
print(m.group(2).strip() + '\n')
print('## Update')
print('Over the air: Settings → Maintenance → "Open update page", upload `firmware.bin` as **Firmware** and `spiffs.bin` as **Filesystem**. Always update both.\n')
print('First installation over USB: see "Flashing the firmware" in the README (boot loader files in `bootloader.zip`).')
PY

if [ $UPDATE = 1 ]; then
  echo "Update of release $TAG: files and notes are replaced, the tag moves to commit ${COMMIT:0:7}:"
else
  echo "Release $TAG for commit ${COMMIT:0:7}:"
fi
echo "-----"
cat "$NOTES"
echo "-----"
read -r -p "Upload? [y/N] " answer
[ "$answer" = "y" ] || [ "$answer" = "Y" ] || { echo "cancelled"; exit 1; }

if [ $UPDATE = 1 ]; then
  git tag -f "$TAG" "$COMMIT" >/dev/null
  git push -f -q origin "refs/tags/$TAG"
  gh release upload "$TAG" "$DIR/firmware.bin" "$DIR/spiffs.bin" doc/bootloader.zip -R "$REPO" --clobber
  gh release edit "$TAG" -R "$REPO" --notes-file "$NOTES"
  echo "https://github.com/$REPO/releases/tag/$TAG updated"
else
  gh release create "$TAG" "$DIR/firmware.bin" "$DIR/spiffs.bin" doc/bootloader.zip \
    -R "$REPO" --target "$COMMIT" --title "$TAG" --notes-file "$NOTES" --latest
fi
