#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
receiver="$repo_root/build/x86-ground/waybeam-link"
streamer="$repo_root/build/x86-ground/frame_shm_feed"
config="${1:-$repo_root/deploy/ground-ubuntu-8812au.json}"
ring="wblink_ground"
log="${WBLINK_GROUND_LOG:-/tmp/waybeam-link-ground.log}"

for path in "$receiver" "$streamer" "$config"; do
	if [[ ! -x "$path" && "$path" != "$config" ]] ||
	   [[ "$path" == "$config" && ! -r "$path" ]]; then
		printf 'required file not available: %s\n' "$path" >&2
		exit 1
	fi
done
command -v ffplay >/dev/null || {
	printf 'ffplay is required (install the ffmpeg package)\n' >&2
	exit 1
}

sudo -v
sudo -- bash -c 'exec "$@"' _ "$receiver" rx -c "$config" >"$log" 2>&1 &
receiver_pid=$!

cleanup() {
	sudo kill -TERM "$receiver_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

ready=0
for _ in $(seq 1 100); do
	if [[ -e "/dev/shm/$ring" ]]; then
		ready=1
		break
	fi
	if ! kill -0 "$receiver_pid" 2>/dev/null; then
		cat "$log" >&2
		exit 1
	fi
	sleep 0.1
done
if [[ "$ready" != 1 ]]; then
	printf 'receiver did not create its frame ring; see %s\n' "$log" >&2
	exit 1
fi

"$streamer" stream "$ring" |
	ffplay -hide_banner -loglevel warning -flags low_delay \
		-framedrop -sync video -analyzeduration 1000000 -probesize 2000000 \
		-f hevc -framerate 55 -i pipe:0