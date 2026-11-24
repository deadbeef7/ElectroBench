#!/usr/bin/env bash
# ===========================================================================
# Run the Android port on the CI emulator and keep everything a crash leaves
# behind. Called by .github/workflows/android-emulator.yml from inside
# android-emulator-runner's `script`, i.e. with a booted device on adb.
#
# This is a separate file on purpose: the action executes each line of `script`
# as its own `sh -c`, so a multi-line script there cannot keep a variable.
#
# What it collects, and why: the app writes portlog.txt/stdout.txt into its
# external files dir, and the port's own log names the render phase a fatal
# signal landed in - but a phone that "goes back to the launcher" leaves that
# log on the phone. So the emulator's crash buffer, tombstones, DropBox crash
# records and ActivityManager's exit info are pulled too, and those outlive the
# process. Screenshots of the device show whether the app was drawing or had
# already fallen back to the launcher.
# ===========================================================================
set -euo pipefail

APK=ports/ElectroBench-android.apk
PKG=org.deadbeef7.electrobench
RUN_DIR=/sdcard/Android/data/$PKG/files
OUT=emulator-artifacts
mkdir -p "$OUT"

adb wait-for-device
# getprop can answer before the framework is up, unlike the runner's own boot
# wait, so poll the boot flag as well.
for _ in $(seq 1 120); do
  [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] && break
  sleep 2
done

{
  echo "apk:      $(sha256sum "$APK")"
  echo "sdk:      $(adb shell getprop ro.build.version.sdk | tr -d '\r')"
  echo "release:  $(adb shell getprop ro.build.version.release | tr -d '\r')"
  echo "abi:      $(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
  echo "product:  $(adb shell getprop ro.product.model | tr -d '\r')"
} | tee "$OUT/device.txt"

# google_apis (not playstore) images allow this, and root is what makes
# /sdcard/Android/data and /data/tombstones readable on API 30+.
adb root || true
adb wait-for-device
sleep 2
adb shell id >> "$OUT/device.txt" || true

# Keep enough buffer that a crash early in the scene still survives to the end
# of the run.
adb logcat -G 16M || true
adb logcat -c || true

adb install -r "$APK" | tee "$OUT/install.txt"
adb shell pm list packages | grep -qx "package:$PKG"

# The port reads getExternalFilesDir()/electrobench.txt on launch. only=1 runs
# the dusk ocean scene alone; the shots are the times at which it also writes a
# PNG into that same directory.
adb shell mkdir -p "$RUN_DIR"
printf 'only=1\nshots=45.1,46.5\n' \
  | adb shell "cat > $RUN_DIR/electrobench.txt" || \
  echo "::warning::could not write the run spec; the port will use its defaults (both scenes)"

adb shell am start -W -n "$PKG/android.app.NativeActivity" | tee "$OUT/launch.txt"

# The benchmark is wall-clock: 45 s scene plus a 12 s results screen. Poll until
# it finishes, or until the process is gone (a crash to the launcher, which is
# the case this run exists for).
finished=0
for i in $(seq 1 120); do
  if adb shell "grep -q 'Benchmark Results' '$RUN_DIR/stdout.txt'" 2>/dev/null; then
    finished=1
    break
  fi
  if ! adb shell pidof "$PKG" >/dev/null 2>&1; then
    if adb shell "grep -q 'benchmark finished' '$RUN_DIR/stdout.txt'" 2>/dev/null; then
      finished=1
    fi
    break
  fi
  # Every ~16 s, photograph the device: it shows the app drawing, or the
  # launcher it fell back to.
  if [ $((i % 8)) -eq 0 ]; then
    adb exec-out screencap -p > "$OUT/screen-$i.png" || rm -f "$OUT/screen-$i.png"
  fi
  sleep 2
done
echo "benchmark finished: $finished"

# ---- everything that survives the process ---------------------------------
adb wait-for-device
adb pull "$RUN_DIR" "$OUT/$PKG" 2>&1 | tail -3 | tee "$OUT/pull.txt" || true
adb logcat -d > "$OUT/logcat.txt" || true
adb logcat -d -b crash > "$OUT/logcat-crash.txt" || true
adb logcat -d -s ElectroBench:V > "$OUT/logcat-electrobench.txt" || true
adb shell dumpsys activity exit-info "$PKG" > "$OUT/exit-info.txt" 2>&1 || true
adb shell dumpsys dropbox --print data_app_crash > "$OUT/dropbox-crash.txt" 2>&1 || true
adb shell dumpsys dropbox --print data_app_anr > "$OUT/dropbox-anr.txt" 2>&1 || true
adb shell 'ls -l /data/tombstones 2>/dev/null; for f in /data/tombstones/tombstone_*; do
             echo "== $f"; cat "$f"; done' > "$OUT/tombstones.txt" 2>&1 || true
ls -lR "$OUT"
