# Boot time: GK7205V200 camera + Hi3536DV100 ground (OpenIPC)

How the power-on -> HDMI picture time was cut on the GK7205 camera and the
Hi3536 ground, and how to reproduce it on a unit freshly flashed with stock
OpenIPC. Written 2026-11-01 for AI agents and operators. Measurements are from
one bench pair (camera `192.168.50.247`, ground `192.168.51.233`).

## Result

| Measurement | Before | After |
|---|---|---|
| Camera reboot -> picture (ground already up) | 27.1 s | 12.1-13.2 s |
| Camera, kernel start -> video on air (cold power-on) | - | ~4.5 s |
| RTL8812AU bring-up per adapter (`hal.total`) | 8.6 s | 1.2-1.3 s |
| Ground reboot -> picture | 26.5 s | 25.5 s |
| Both powered together -> picture (operator, stopwatch) | - | ~26 s |

Powered together, the **ground is the bottleneck**: ~10.5 s in U-Boot/hardware
before the kernel starts (not reducible from Linux; needs a serial log), 2 s
kernel, ~3 s init, then radios + decoder. The camera is ready before that. The
ground reboot figure includes ~4.9 s of shutdown that power-on does not have.
The monitor's HDMI lock adds a few seconds that these numbers do not include.

## What costs time, and the fix

| # | Where | Cause | Fix | Saves |
|---|---|---|---|---|
| 1 | both | 10 ms-tick kernel: every `usleep(1)` / `nanosleep` rounds up to 10 ms; devourer's `ODM_delay_us(1)` runs thousands of times in `RadioAir::create` | link-time wrapper of libc `nanosleep` that spins requests <= 200 µs, active only inside `RadioAir::create` (`app/precise_sleep_wrap.cpp`, `io/src/precise_sleep.cpp`) | ~7 s per adapter |
| 2 | both | `S40network` runs `ifup eth0` in the foreground; udhcpc discover retries hold the boot | run `ifup eth0` in a background subshell | 1.3 s (ground) to 6.5 s (camera, or no cable) |
| 3 | both | ~7.9 KB of kernel log at 115200 baud | append `quiet` to U-Boot `bootargs` | ~0.6 s |
| 4 | camera | the radio started only after the encoder's first frame (waybeam spawned it from `/etc/waybeam-children.json`) | `S39waybeam-link` starts the radio early with a 0.2 s respawn loop; set the child `"enabled": false` | ~1 s |
| 5 | camera | OpenIPC `S95majestic`, `S98datalink`, `S98vtun` start nothing here | remove them (only if majestic / wifibroadcast / vtun are absent) | ~0.15 s |
| 6 | ground | the link started at S98 after the decoder | `S71waybeam-link`, right after the MPP modules (`S70vendor`) load | ~0.4 s |

Two things that look like improvements and are not:

- The spin cap must stay small. A 10 ms cap broke chip power-on
  (`rtw_read(0005)` errors); 200 µs works. The millisecond power-sequence waits
  must keep their tick-rounded length.
- Starting the ground link **before** `S70vendor` (e.g. S41) makes the two-adapter
  bring-up ~2x slower (7.3 s vs 3.3 s): it competes with the MPP module load on
  the single core. S71 is the earliest safe slot.

Camera warm-reboot quirk: the dongle keeps VBUS across a warm reboot and can
re-enumerate once during the first bring-up (~2 s after boot). The 0.2 s
respawn loop absorbs it (~2 s of the 13 s). Cold power-on has no dropout.
Do not gate the start on uptime; it was tried and is inconsistent.

## Reproduce on a fresh OpenIPC unit

Prerequisites: the waybeam-link build must contain items 1 (check
`grep -c init.timing /usr/bin/waybeam-link` > 0 on the device), and the unit
must already run waybeam (camera) / waybeam-vdec (ground) with their configs.

1. Optimise both units, from this repo:

   ```sh
   deploy/waybeam-boot-optimize.sh                 # camera and ground
   deploy/waybeam-boot-optimize.sh --camera-only   # or --ground-only
   CAMERA=root@<ip> GROUND=root@<ip> SSHPASS=<pw> deploy/waybeam-boot-optimize.sh
   ```

   It first backs up each unit's `/etc` and U-Boot env to
   `./waybeam-boot-backup/<camera|ground>-<date>/` on the host (nothing is
   written to the devices' flash), then applies items 2-6. It edits the stock
   `S40network` in place with `sed` instead of overwriting it, is idempotent, and
   reports an old link build. It installs no Waybeam software.
2. Reboot both. Undo with `deploy/waybeam-boot-optimize.sh --restore` (uses the
   oldest backup).
3. For the project's own units, the same files are installed by the repos'
   deploy scripts: `waybeam/deploy-gk7205.sh` (S39, S40network, rcS, children
   json, removal of idle services, `quiet`) and
   `hi3536dv100-ground/deploy-hi3536-ground.sh` (S71). The script embeds compact
   copies of `S39waybeam-link` and `S71waybeam-link`; keep their behaviour in sync with
   `waybeam/init.d/S39waybeam-link` and `hi3536dv100-ground/init.d/S71waybeam-link`.

Manual equivalent, per unit (BusyBox, `ash`):

```sh
# DHCP in the background (S40network: the line that runs ifup eth0)
sed -i -E 's/^([[:space:]]+)ifup eth0( \$file)?$/\1(ifup eth0\2 <\/dev\/null >\/dev\/null 2>\&1 \&)/' /etc/init.d/S40network
fw_setenv bootargs "$(fw_printenv -n bootargs) quiet"
```

Camera: install `S39waybeam-link` (respawn loop around
`waybeam-link tx -c /etc/waybeam-link/craft.json`), set `"enabled": false` in
`/etc/waybeam-children.json`, delete the three idle OpenIPC scripts.
Ground: install `S71waybeam-link` (starts `waybeam-link rx -c
/etc/waybeam-link/ground.json` unless running; `S98vdec` skips a running link
and still stops it). The ground needs the link config's two adapters selected
by role / USB bus port, never by MAC.

## Measure

- `waybeam/tools/boot-measure.sh <label>`: reboots the camera and prints each
  stage on the host clock up to the first picture. Needs the `rcS` trace
  (`/tmp/boot.trace`, RAM only; `waybeam/init.d/rcS`) and the ground vdec's
  `video: displayed at mono N ms after M ms without` line. Bench only: it
  reboots the camera.
- Ground `waybeam-vdec` stage lines carry CLOCK_MONOTONIC ms
  (`stage ... (mono N ms)`), equal to uptime.
- Radio bring-up detail: the link log lines `radio: init.timing ...`
  (`bb_config`, `rf_config`, `hal.total`).
- Time kernel start from the host as `host_time_at_ssh - /proc/uptime`.

## Not done / next

- Ground U-Boot/hardware, ~10.5 s before the kernel: needs a serial console
  capture of a power-on (`bootdelay=0`, vendor U-Boot 2010.06, `sf read`).
- The two ground adapters are brought up one after the other; doing them in
  parallel inside `RadioAir::create` would save ~1.6 s.
- Unknown: whether the 200 µs spin cap can trip the `rtw_read(0005)` failure on
  other dies (one bench, two dies of one part).

Findings: `docs/findings.md` (2026-11-01).
