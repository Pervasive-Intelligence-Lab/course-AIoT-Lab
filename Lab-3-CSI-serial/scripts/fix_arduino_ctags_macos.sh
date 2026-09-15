#!/bin/bash
# Build Arduino's ctags 5.8-arduino11 for Apple Silicon and back up the old tool.
set -euo pipefail

if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
  echo "Run this script in a native Apple Silicon macOS terminal." >&2
  exit 1
fi

# Override only to repair/test a different installation of the same version.
ctags_tool_dir="${ARDUINO_CTAGS_DIR:-$HOME/Library/Arduino15/packages/builtin/tools/ctags/5.8-arduino11}"
ctags_target="$ctags_tool_dir/ctags"
if [[ ! -f "$ctags_target" ]]; then
  echo "Arduino ctags was not found at: $ctags_target" >&2
  echo "Check the path in your Arduino compilation error before proceeding." >&2
  exit 1
fi

file "$ctags_target"
case "$(file -b "$ctags_target")" in
  *arm64*)
    if "$ctags_target" --version; then
      echo "Arduino ctags already runs natively on ARM64. No replacement needed."
      exit 0
    fi
    ;;
esac

for ctags_command in python3 curl tar make xcrun; do
  if ! command -v "$ctags_command" >/dev/null 2>&1; then
    echo "Missing command: $ctags_command" >&2
    echo "Install the missing prerequisite and rerun this script." >&2
    exit 1
  fi
done
if ! xcrun --find clang >/dev/null 2>&1; then
  echo "Install Apple's developer tools with: xcode-select --install" >&2
  echo "Finish the installation, then rerun this script." >&2
  exit 1
fi

ctags_build_dir="$(mktemp -d "${TMPDIR:-/tmp}/arduino-ctags-arm64.XXXXXX")"
ctags_staged=""
cleanup() {
  ctags_exit_status=$?
  if [[ -n "$ctags_staged" ]]; then
    rm -f "$ctags_staged"
  fi
  if [[ "$ctags_exit_status" -eq 0 ]]; then
    rm -rf "$ctags_build_dir"
  else
    echo "Build failed; diagnostic files retained in: $ctags_build_dir" >&2
  fi
}
trap cleanup EXIT

curl --fail --location --retry 2 --connect-timeout 15 --max-time 120 \
  https://codeload.github.com/arduino/ctags/tar.gz/refs/tags/5.8-arduino11 \
  --output "$ctags_build_dir/source.tar.gz"
tar -xzf "$ctags_build_dir/source.tar.gz" --strip-components=1 -C "$ctags_build_dir"
cd "$ctags_build_dir"

# Avoid reserved attribute macro names that conflict with newer macOS SDKs.
# Work with bytes because some files in this older source tree are not UTF-8.
python3 - <<'PY'
from pathlib import Path

for path in Path('.').iterdir():
    if path.is_file() and path.suffix in {'.c', '.h'}:
        original = path.read_bytes()
        updated = original.replace(b'__unused__', b'CTAGS_UNUSED')
        updated = updated.replace(b'__printf__', b'CTAGS_PRINTF')
        if updated != original:
            path.write_bytes(updated)
PY

./configure CC='xcrun clang' \
  CFLAGS='-O2 -arch arm64 -std=gnu89 -Wno-error=implicit-function-declaration'
make -j4
file ./ctags
./ctags --version

ctags_backup="$ctags_tool_dir/ctags.x86_64.backup"
if [[ ! -e "$ctags_backup" ]]; then
  cp -p "$ctags_target" "$ctags_backup"
fi
ctags_staged="$(mktemp "$ctags_tool_dir/.ctags-arm64.XXXXXX")"
cp ./ctags "$ctags_staged"
chmod 755 "$ctags_staged"
mv -f "$ctags_staged" "$ctags_target"
ctags_staged=""

file "$ctags_target"
"$ctags_target" --version
echo "Installed ARM64 Arduino ctags: $ctags_target"
echo "Original tool backup: $ctags_backup"
echo "Return to Arduino IDE and click Verify."
