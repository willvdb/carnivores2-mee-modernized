#!/usr/bin/env bash
# Run only against a new, disposable Xorg dummy server; never use the host DISPLAY.
set -euo pipefail
test_binary=$(realpath "${1:?Usage: $0 /path/to/Carnivores2SDLX11ModeLeakTest}")
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
scratch=$(mktemp -d)
xorg_pid=
wm_pid=
cleanup() {
    result=$?
    trap - EXIT
    if [[ -n "$wm_pid" ]]; then kill "$wm_pid" 2>/dev/null || true; wait "$wm_pid" 2>/dev/null || true; fi
    if [[ -n "$xorg_pid" ]]; then kill "$xorg_pid" 2>/dev/null || true; wait "$xorg_pid" 2>/dev/null || true; fi
    if (( result != 0 )); then
        cat "$scratch"/*.log 2>/dev/null || true
        printf 'X11 regression logs retained at %s\n' "$scratch"
    else
        rm -rf -- "$scratch"
    fi
    exit "$result"
}
trap cleanup EXIT
# Bypass distributions' console-user-only Xorg wrapper. No root is needed by
# the dummy driver; no physical GPU, input device, or VT is attached.
xorg=/usr/lib/xorg/Xorg
if [[ ! -x "$xorg" ]]; then xorg=/usr/lib/Xorg; fi
"$xorg" -displayfd 3 -noreset -nolisten tcp -config "$script_dir/mode-leak-xorg.conf" \
    -logfile "$scratch/Xorg.log" 3>"$scratch/display" >"$scratch/server.log" 2>&1 &
xorg_pid=$!
for ((i=0; i<100; ++i)); do
    [[ -s "$scratch/display" ]] && break
    kill -0 "$xorg_pid"
    sleep 0.1
done
[[ -s "$scratch/display" ]]
export DISPLAY=":$(cat "$scratch/display")"
export SDL_VIDEODRIVER=x11
openbox >"$scratch/openbox.log" 2>&1 &
wm_pid=$!
for ((i=0; i<100; ++i)); do
    wm_state=$(xprop -root _NET_SUPPORTING_WM_CHECK)
    [[ "$wm_state" == *"window id"* ]] && break
    kill -0 "$wm_pid"
    sleep 0.1
done
[[ "$wm_state" == *"window id"* ]]
xrandr --addmode DUMMY1 1280x1024
xrandr --output DUMMY0 --primary --output DUMMY1 --mode 1280x1024 --pos 1920x0
xrandr --addmode DUMMY1 800x600
for ((i=0; i<100; ++i)); do
    topology=$(xrandr --query)
    [[ "$topology" == *"DUMMY1 connected 1280x1024+1920+0"* ]] && break
    sleep 0.1
done
[[ "$topology" == *"DUMMY1 connected 1280x1024+1920+0"* ]]
xrandr --listmonitors
timeout 60 "$test_binary" 0 20
timeout 60 "$test_binary" 1 20
# Verify the server's state too, independently of SDL's cached desktop mode.
topology=$(xrandr --query)
[[ "$topology" == *"DUMMY0 connected primary 1920x1080+0+0"* ]]
[[ "$topology" == *"DUMMY1 connected 1280x1024+1920+0"* ]]
