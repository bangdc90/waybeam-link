#!/usr/bin/env bash
# waybeam-boot-optimize.sh — shorten power-on -> picture on a Waybeam FPV pair:
# GK7205V200 camera (OpenIPC + waybeam + waybeam-link) and Hi3536DV100 ground
# (OpenIPC + waybeam-link + waybeam-vdec). Boot-time changes only; it installs
# no Waybeam software.
#
#   ./waybeam-boot-optimize.sh                 optimise camera and ground
#   ./waybeam-boot-optimize.sh --camera-only   (or --ground-only)
#   ./waybeam-boot-optimize.sh --restore       undo: back to the first backup taken
#
#   CAMERA=root@192.168.50.247 GROUND=root@192.168.51.233 SSHPASS=12345
#
# Before touching a device it copies /etc and the U-Boot environment to
# ./waybeam-boot-backup/<camera|ground>-<date>/ on THIS computer (nothing is
# stored on the devices). Reboot both afterwards.
#
# What it changes, and why (measured 2026-11-01):
#   both    S40network: DHCP in the background. udhcpc's discover retries held
#           the rest of the boot 1.3 s (ground) to 6.5 s (camera, or no cable).
#   both    kernel args + "quiet": ~7.8 KB of boot log no longer crawls out of
#           the 115200-baud console (~0.6 s).
#   camera  S39waybeam-link: the radio starts at once instead of after the
#           encoder's first frame (the waybeam child that used to spawn it is
#           disabled), and restarts 0.2 s after any exit.
#   camera  S95majestic / S98datalink / S98vtun removed when they have nothing
#           to start (no majestic, no wifibroadcast, no vtun set).
#   ground  S71waybeam-link: the radios start right after the MPP modules
#           load instead of at S98, overlapping the decoder's start-up;
#           S98vdec already skips a link that is running and still stops it.
#           (Earlier than S70 the module load slows the USB bring-up 2x.)
#
# The biggest single saving is not here: waybeam-link builds from 2026-11-01
# on bring an RTL8812AU up in ~1.3 s instead of ~8.6 s on these 10 ms-tick
# kernels. The script reports whether the installed link has it.
set -euo pipefail

CAMERA="${CAMERA:-root@192.168.50.247}"
GROUND="${GROUND:-root@192.168.51.233}"
export SSHPASS="${SSHPASS:-12345}"
BACKUP_ROOT="${BACKUP_ROOT:-$PWD/waybeam-boot-backup}"
DO_CAMERA=1
DO_GROUND=1
RESTORE=0
for a in "$@"; do
    case "$a" in
        --camera-only) DO_GROUND=0 ;;
        --ground-only) DO_CAMERA=0 ;;
        --restore) RESTORE=1 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done
command -v sshpass >/dev/null || { echo "needs sshpass (apt install sshpass)" >&2; exit 1; }

log() { printf '[%s] %s\n' "$TARGET" "$*"; }
remote() { sshpass -e ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 -o LogLevel=ERROR "$HOST" "$@"; }
put() {  # local-file remote-path mode
    remote "cat > '$2.new' && chmod $3 '$2.new' && mv -f '$2.new' '$2'" < "$1"
}

backup() {
    local dir="$BACKUP_ROOT/$TARGET-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$dir"
    remote 'tar -cf - -C / etc' | gzip -c > "$dir/etc.tar.gz"
    remote 'fw_printenv' > "$dir/uboot-env.txt"
    tar -tzf "$dir/etc.tar.gz" >/dev/null && grep -q '^bootargs=' "$dir/uboot-env.txt" ||
        { echo "backup of $HOST incomplete, nothing changed" >&2; exit 1; }
    log "backup: $dir"
}

# DHCP in the background: wrap S40network's "ifup eth0[ $file]" line.
network_background() {
    if remote "grep -q 'Waybeam: DHCP in the background' /etc/init.d/S40network"; then
        log "S40network: already backgrounded"
    elif remote "grep -qE '^[[:space:]]+ifup eth0( \\\$file)?\$' /etc/init.d/S40network"; then
        remote "sed -i -E 's/^([[:space:]]+)ifup eth0( \\\$file)?\$/\\1# Waybeam: DHCP in the background (boot time)\\n\\1(ifup eth0\\2 <\\/dev\\/null >\\/dev\\/null 2>\\&1 \\&)/' /etc/init.d/S40network"
        log "S40network: DHCP now in the background"
    else
        log "S40network: unknown layout, left alone"
    fi
}

kernel_quiet() {
    if remote 'fw_printenv -n bootargs' | grep -qw quiet; then
        log "bootargs: already quiet"
    else
        remote 'fw_setenv bootargs "$(fw_printenv -n bootargs) quiet"'
        log "bootargs: + quiet"
    fi
}

check_link_build() {
    if remote 'grep -q init.timing /usr/bin/waybeam-link 2>/dev/null'; then
        log "waybeam-link: has the fast radio bring-up"
    else
        log "waybeam-link: OLD build — radio bring-up stays ~8.6 s per adapter; update waybeam-link for the main saving"
    fi
}

camera_link_script() {
    cat <<'EOF'
#!/bin/sh
# Waybeam boot layout: start waybeam-link (the radio) early, in parallel with
# the network and the encoder; it attaches to the encoder's frame ring when it
# appears. The waybeam child in /etc/waybeam-children.json is disabled so one
# process owns the adapter. A loop restarts the link 0.2 s after any exit (a
# warm reboot can make the dongle drop off USB once on the first bring-up).
# Create /etc/waybeam.disable to skip, as for S96waybeam.
DAEMON="waybeam-link"
DAEMON_BIN="/usr/bin/$DAEMON"
CONFIG="/etc/waybeam-link/craft.json"
PIDFILE="/var/run/$DAEMON-loop.pid"
LOGFILE="/tmp/$DAEMON.log"

start() {
	[ -f /etc/waybeam.disable ] && { echo "Skipping $DAEMON"; return 0; }
	[ -x "$DAEMON_BIN" ] || { echo "Skipping $DAEMON: $DAEMON_BIN missing"; return 1; }
	echo -n "Starting $DAEMON: "
	start-stop-daemon -b -m -S -q -p "$PIDFILE" -x /bin/sh -- -c \
		"while :; do [ -f $LOGFILE ] && [ \$(wc -c < $LOGFILE) -gt 1048576 ] && : > $LOGFILE; $DAEMON_BIN tx -c $CONFIG </dev/null >>$LOGFILE 2>&1; usleep 200000; done" &&
		echo "OK" || echo "FAIL"
}

stop() {
	echo -n "Stopping $DAEMON: "
	start-stop-daemon -K -q -p "$PIDFILE" 2>/dev/null
	rm -f "$PIDFILE"
	killall -TERM "$DAEMON" 2>/dev/null
	i=0
	while pidof "$DAEMON" >/dev/null 2>&1 && [ $i -lt 30 ]; do usleep 100000; i=$((i + 1)); done
	echo "OK"
}

case "$1" in
	start|stop) $1 ;;
	restart|reload) stop; start ;;
	*) echo "Usage: $0 {start|stop|restart|reload}"; exit 1 ;;
esac
EOF
}

ground_link_script() {
    cat <<'EOF'
#!/bin/sh
# Waybeam boot layout: start the ground waybeam-link (both radios) right after
# the MPP modules load, so its bring-up overlaps the decoder's start-up rather
# than following it. S98vdec starts only what is not running yet and stops
# both, so this script has no stop of its own.
LINK_BIN=/usr/bin/waybeam-link
GROUND_CONFIG=/etc/waybeam-link/ground.json
RUN_DIR=/var/run/waybeam-ground
LOG_DIR=/tmp/waybeam-ground

case "$1" in
	start)
		[ -x "$LINK_BIN" ] && [ -r "$GROUND_CONFIG" ] || exit 0
		start-stop-daemon -K -t -q -x "$LINK_BIN" && exit 0
		mkdir -p "$RUN_DIR" "$LOG_DIR"
		start-stop-daemon -S -q -m -p "$RUN_DIR/rx.pid" -x "$LINK_BIN" -- \
			rx -c "$GROUND_CONFIG" >>"$LOG_DIR/rx.log" 2>&1 &
		;;
	stop|restart) ;;
	*) echo "Usage: $0 {start}"; exit 1 ;;
esac
EOF
}

optimise_camera() {
    backup
    network_background
    kernel_quiet
    local tmp; tmp="$(mktemp)"
    camera_link_script > "$tmp"; put "$tmp" /etc/init.d/S39waybeam-link 755; rm -f "$tmp"
    log "S39waybeam-link installed"
    if remote '[ -f /etc/waybeam-children.json ]'; then
        remote "sed -i 's/\"enabled\": *true/\"enabled\": false/' /etc/waybeam-children.json"
        log "waybeam-children.json: waybeam no longer spawns the link"
    fi
    remote '[ -f /etc/init.d/S95majestic ] && ! command -v majestic >/dev/null && rm -f /etc/init.d/S95majestic; [ -f /etc/init.d/S98datalink ] && ! command -v wifibroadcast >/dev/null && rm -f /etc/init.d/S98datalink; [ -f /etc/init.d/S98vtun ] && [ -z "$(fw_printenv -n vtun 2>/dev/null)" ] && rm -f /etc/init.d/S98vtun; true'
    log "idle OpenIPC services removed (majestic / wfb datalink / vtun, only when unused)"
    check_link_build
}

optimise_ground() {
    backup
    network_background
    kernel_quiet
    local tmp; tmp="$(mktemp)"
    ground_link_script > "$tmp"; put "$tmp" /etc/init.d/S71waybeam-link 755; rm -f "$tmp"
    remote 'rm -f /etc/init.d/S41waybeam-link'
    log "S71waybeam-link installed (S98vdec unchanged)"
    check_link_build
}

restore() {
    local dir
    dir="$(ls -d "$BACKUP_ROOT/$TARGET-"* 2>/dev/null | sort | head -1 || true)"
    [ -n "$dir" ] || { log "no backup in $BACKUP_ROOT"; return 1; }
    local tmp; tmp="$(mktemp -d)"
    tar -xzf "$dir/etc.tar.gz" -C "$tmp"
    remote 'rm -f /etc/init.d/S39waybeam-link /etc/init.d/S41waybeam-link /etc/init.d/S71waybeam-link'
    for f in S40network S95majestic S98datalink S98vtun; do
        [ -f "$tmp/etc/init.d/$f" ] && put "$tmp/etc/init.d/$f" "/etc/init.d/$f" 755
    done
    [ -f "$tmp/etc/waybeam-children.json" ] && put "$tmp/etc/waybeam-children.json" /etc/waybeam-children.json 644
    local orig; orig="$(grep '^bootargs=' "$dir/uboot-env.txt" | cut -d= -f2-)"
    remote "fw_setenv bootargs '$orig'"
    rm -rf "$tmp"
    log "restored from $dir (the oldest backup = before the first optimisation); reboot to apply"
}

for TARGET in camera ground; do
    if [ "$TARGET" = camera ]; then [ $DO_CAMERA = 1 ] || continue; HOST="$CAMERA"; fi
    if [ "$TARGET" = ground ]; then [ $DO_GROUND = 1 ] || continue; HOST="$GROUND"; fi
    remote true || { echo "cannot reach $TARGET at $HOST" >&2; exit 1; }
    if [ $RESTORE = 1 ]; then restore; else "optimise_$TARGET"; fi
done
echo "Done. Reboot camera and ground for the new boot to take effect."
