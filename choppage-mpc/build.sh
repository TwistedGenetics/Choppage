#!/bin/sh
# Builds the CHOPPAGE MPC plugin folder with the official MPC Plugin SDK (external/mpc-plugin-builder-sdk):
#   native (for the host simulator) + arm32 (Gen1: One / One+ / Retro / Live II / X / Key / Force) + arm64 (Gen2).
# Output: choppage-mpc/build/Twisted Genetics - VST - CHOPPAGE/   -> copy into <drive>/VST2 Plugins/
#
#   choppage-mpc/build.sh [extra build.py args, e.g. --target native --toolchain gcc]
#   CHOP_MPC_SDK=/path/to/sdk choppage-mpc/build.sh    (use an SDK checkout somewhere else)
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
sdk="${CHOP_MPC_SDK:-$root/external/mpc-plugin-builder-sdk}"
[ -f "$sdk/tools/build.py" ] || { echo "MPC Plugin SDK missing: git submodule update --init external/mpc-plugin-builder-sdk"; exit 1; }
py="${PYTHON:-python3}"

# 1. one wrapper per core source (build.py compiles src/*.cpp only)
"$here/tools/sync_core.sh" >/dev/null

# 2. SDK build; core and app headers come in through CPATH (build.py only adds src/ and the framework)
export CPATH="$root/choppage-core/include:$here/include${CPATH:+:$CPATH}"
"$py" "$sdk/tools/build.py" "$here" "$@"

# 3. The touch UI is the S950 skin in choppage-mpc/skins/ (made by tools/make_skin.py, committed), which build.py
#    copies verbatim. Do not re-save TUI.json with json.dump: that drops the duplicate "version" keys the overlay
#    widgets need, and the MPC then rejects the whole file.
