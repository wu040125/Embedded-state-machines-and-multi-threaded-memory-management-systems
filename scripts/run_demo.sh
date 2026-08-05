#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-out/build/linux-gcc-debug}"
socket_path="/tmp/edge-sentinel-demo-$$.sock"

"${build_dir}/edge_sentinel_daemon" --socket "${socket_path}" &
daemon_pid=$!

cleanup() {
    if kill -0 "${daemon_pid}" 2>/dev/null; then
        kill "${daemon_pid}" 2>/dev/null || true
        wait "${daemon_pid}" 2>/dev/null || true
    fi
    rm -f "${socket_path}"
}
trap cleanup EXIT

for _ in $(seq 1 50); do
    [[ -S "${socket_path}" ]] && break
    sleep 0.1
done

ctl=("${build_dir}/edgectl" --socket "${socket_path}")
"${ctl[@]}" status
"${ctl[@]}" inject temperature 105
sleep 0.5
"${ctl[@]}" status
"${ctl[@]}" metrics
"${ctl[@]}" clear
"${ctl[@]}" reset
sleep 0.2
"${ctl[@]}" status
"${ctl[@]}" shutdown
wait "${daemon_pid}"
trap - EXIT
rm -f "${socket_path}"
