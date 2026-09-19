#!/usr/bin/env bash
# No host display, DRM device, input device, or VT is used.
set -euo pipefail
binary=$(realpath "${1:?Usage: $0 /path/to/Carnivores2SDLWaylandPresentationTest}")
disconnect_binary=$(realpath "${2:?Supply /path/to/Carnivores2SDLWaylandDisconnectTest}")
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
scratch=$(mktemp -d)
xorg_pid= weston_pid=
cleanup() {
    result=$?
    trap - EXIT
    for pid in "$weston_pid" "$xorg_pid"; do
        if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    done
    if (( result )); then
        cat "$scratch"/*.log 2>/dev/null || true
        printf 'Wayland regression logs retained at %s\n' "$scratch"
    else rm -rf -- "$scratch"; fi
    exit "$result"
}
trap cleanup EXIT
mkdir "$scratch/runtime"
chmod 700 "$scratch/runtime"
xorg=/usr/lib/xorg/Xorg
[[ -x "$xorg" ]] || xorg=/usr/lib/Xorg
"$xorg" -displayfd 3 -noreset -nolisten tcp -config "$script_dir/../x11/mode-leak-xorg.conf" \
    -logfile "$scratch/Xorg.log" 3>"$scratch/display" >"$scratch/server.log" 2>&1 &
xorg_pid=$!
for ((i=0;i<100;++i)); do
    [[ -s "$scratch/display" ]] && break
    kill -0 "$xorg_pid"
    sleep .1
done
[[ -s "$scratch/display" ]]
export DISPLAY=":$(cat "$scratch/display")"
export XDG_RUNTIME_DIR="$scratch/runtime"
export WAYLAND_DISPLAY=carnivores-test
weston --backend=x11-backend.so --use-pixman --width=1280 --height=1024 --output-count=2 \
    --socket="$WAYLAND_DISPLAY" --idle-time=0 --no-config --log="$scratch/weston.log" \
    >"$scratch/weston-console.log" 2>&1 &
weston_pid=$!
for ((i=0;i<100;++i)); do
    [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]] && break
    kill -0 "$weston_pid"
    sleep .1
done
[[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]
export SDL_VIDEODRIVER=wayland CARNIVORES_TEST_WAYLAND_PRESENTATION=1
timeout --kill-after=5 60 "$binary"
timeout --kill-after=2 10 "$disconnect_binary"
