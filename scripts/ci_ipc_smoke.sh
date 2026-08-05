#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:?build directory is required}"
socket_path="/tmp/edge-sentinel-ci-$$.sock"
daemon_log="${build_dir}/edge-sentinel-daemon.log"

"${build_dir}/edge_sentinel_daemon" --socket "${socket_path}" >"${daemon_log}" 2>&1 &
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
    if [[ -S "${socket_path}" ]]; then
        break
    fi
    sleep 0.1
done

[[ -S "${socket_path}" ]]
"${build_dir}/edgectl" --socket "${socket_path}" status | grep -q "state=Healthy"
"${build_dir}/edgectl" --socket "${socket_path}" inject temperature 105 | grep -q "ok=injection_set"
sleep 0.5
"${build_dir}/edgectl" --socket "${socket_path}" status | grep -q "state=FaultLatched"
"${build_dir}/edgectl" --socket "${socket_path}" metrics | grep -q "events_processed="
"${build_dir}/edgectl" --socket "${socket_path}" shutdown | grep -q "ok=shutdown_requested"
wait "${daemon_pid}"
trap - EXIT
rm -f "${socket_path}"
