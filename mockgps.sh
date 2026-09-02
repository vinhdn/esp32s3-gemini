#!/usr/bin/env bash
# mockgps.sh — control the GpsMock engine that GpsMock.kt runs inside vn.vietmap.live.
#
# Requires: a rooted device over adb (the flag files live in the app's private dir).
# The Vegvisir LSPosed module must be enabled with vn.vietmap.live in its scope.
#
# Usage:
#   ./mockgps.sh enable [start_lat,lng] [end_lat,lng] [steps] [intervalMs]
#   ./mockgps.sh go          # start moving toward the end point
#   ./mockgps.sh reset       # jump back to the start point (removes mock_go)
#   ./mockgps.sh status
#   ./mockgps.sh disable     # turn the mock off completely
#   ./mockgps.sh logs        # tail the GPS-mock logcat
set -euo pipefail

PKG="vn.vietmap.live"
DIR="/data/data/${PKG}/files"

# Defaults: ~2 km run into chợ Bến Thành, 120 steps × 1 s = 2 min.
DEF_START="10.7592,106.6820"
DEF_END="10.77216,106.69804"
DEF_STEPS="120"
DEF_INTERVAL="1000"

# Run a command as root on the device (handles both `su -c` and adb-root shells).
sh_root() { adb shell "su -c '$*'" 2>/dev/null || adb shell "$*"; }

restart_app() {
  echo ">> force-stop ${PKG} so the enable flag is seen at process start"
  sh_root "am force-stop ${PKG}"
  sleep 1
  echo ">> relaunch ${PKG}"
  sh_root "monkey -p ${PKG} -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1"
}

cmd="${1:-status}"; shift || true

case "$cmd" in
  enable)
    START="${1:-$DEF_START}"; END="${2:-$DEF_END}"
    STEPS="${3:-$DEF_STEPS}"; INTERVAL="${4:-$DEF_INTERVAL}"
    echo ">> writing route: start=${START} end=${END} steps=${STEPS} intervalMs=${INTERVAL}"
    sh_root "mkdir -p ${DIR}"
    sh_root "printf 'start=%s\nend=%s\nsteps=%s\nintervalMs=%s\n' '${START}' '${END}' '${STEPS}' '${INTERVAL}' > ${DIR}/mock_gps.conf"
    sh_root "touch ${DIR}/mock_gps.enable"
    sh_root "rm -f ${DIR}/mock_go"                 # start held at START until you call `go`
    sh_root "chmod 666 ${DIR}/mock_gps.enable ${DIR}/mock_gps.conf"
    restart_app
    echo ">> enabled. Position holds at START; run './mockgps.sh go' to move."
    ;;

  go)
    sh_root "touch ${DIR}/mock_go && chmod 666 ${DIR}/mock_go"
    echo ">> moving toward the end point."
    ;;

  reset)
    sh_root "rm -f ${DIR}/mock_go"
    echo ">> reset: position holds at START again (relaunch or wait one tick)."
    ;;

  status)
    echo ">> flags in ${DIR}:"
    sh_root "ls -l ${DIR}/mock_gps.enable ${DIR}/mock_gps.conf ${DIR}/mock_go 2>/dev/null" || true
    echo ">> conf:"; sh_root "cat ${DIR}/mock_gps.conf 2>/dev/null" || true
    ;;

  disable)
    sh_root "rm -f ${DIR}/mock_gps.enable ${DIR}/mock_gps.conf ${DIR}/mock_go"
    restart_app
    echo ">> disabled — real GPS restored."
    ;;

  logs)
    adb logcat -s VietmapGps
    ;;

  *)
    echo "unknown command: $cmd"; sed -n '3,16p' "$0"; exit 1
    ;;
esac
