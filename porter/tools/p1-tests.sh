#!/usr/bin/env bash
# Runs the execution tests that research-committer-syncs.md lists for the GPU-ordered storage write-backs (P1),
# with the CTest environment of libSceAgcDriver/CMakeLists.txt (ANYPS5_NO_SHADER_CACHE=1, repo root as working
# directory, libc's directory first on PATH) and no other APS5_* variable. Every test runs once with the new
# default and once with APS5_NO_DEFER_STORES=1 (the waits as before). VALIDATE=1 adds the Khronos validation layer
# (ANYPS5_REQUIRE_VULKAN=1, VK_INSTANCE_LAYERS) and counts VUID- lines per test. Usage: p1-tests.sh <out-log>
set -u
repo="C:/Users/Mubeen/Documents/Projects/ps5-porter"
tests="$repo/build/tests"
out="${1:-$repo/build-p1-tests-run.log}"
export PATH="/c/Users/Mubeen/Documents/Projects/ps5-porter/build/core/libs/libs/unpatched:/c/Users/Mubeen/tools/winlibs-15.2.0-r7/mingw64/bin:$PATH"
for name in $(env | grep -o '^APS5_[A-Z0-9_]*\|^ANYPS5_[A-Z0-9_]*'); do unset "$name"; done
cd "$repo" || exit 1
: > "$out"
fails=0
# name|executable|arguments|extra environment|expected result (pass, or fail for WILL_FAIL)
cases=(
  "storage_flush_ordering|agc_driver_storage_flush_ordering_tests|||pass"
  "linear_readback_label|agc_driver_linear_readback_label_tests|||pass"
  "linear_readback_label_off|agc_driver_linear_readback_label_tests||APS5_NO_LABEL_READBACK_STORE=1|fail"
  "depth_plane_sampling|agc_driver_depth_plane_sampling_tests|||pass"
  "depth_plane_sampling_evicting|agc_driver_depth_plane_sampling_tests|--evicting|APS5_DEPTH_PLANE_COPY_MIB=1|pass"
  "depth_plane_buffer_memory|agc_driver_depth_plane_buffer_memory_tests|||pass"
  "host_write_pending_storage|agc_driver_host_write_pending_storage_tests|||pass"
  "copied_writer_label_order|agc_driver_copied_writer_label_order_tests|||pass"
  "copied_writer_label_order_drain|agc_driver_copied_writer_label_order_tests||APS5_DRAIN_COMPLETION_LABELS=1|pass"
  "storage_read_during_idle_store|agc_driver_storage_read_during_idle_store_tests|||pass"
)
for mode in default no-defer; do
  for entry in "${cases[@]}"; do
    IFS='|' read -r name exe args extra expect <<< "$entry"
    envs=(ANYPS5_NO_SHADER_CACHE=1)
    [ -n "$extra" ] && envs+=("$extra")
    [ "$mode" = no-defer ] && envs+=(APS5_NO_DEFER_STORES=1)
    [ "${VALIDATE:-0}" = 1 ] && envs+=(ANYPS5_REQUIRE_VULKAN=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation)
    start=$(date +%s.%N)
    output=$(env "${envs[@]}" timeout 180 "$tests/$exe.exe" $args 2>&1)
    code=$?
    seconds=$(awk -v a="$start" -v b="$(date +%s.%N)" 'BEGIN { printf "%.1f", b - a }')
    if [ "$code" -eq 77 ]; then verdict=skip
    elif [ "$expect" = pass ] && [ "$code" -eq 0 ]; then verdict=ok
    elif [ "$expect" = fail ] && [ "$code" -ne 0 ]; then verdict=ok
    else verdict=FAIL; fails=$((fails + 1)); fi
    last=$(printf '%s\n' "$output" | grep -v '^\s*$' | grep -v 'VUID-' | tail -1)
    vuids=$(printf '%s\n' "$output" | grep -c 'VUID-')
    printf '%s\n' "$output" | grep -q 'was not found but was requested by env var VK_INSTANCE_LAYERS' && vuids="layer-missing"
    printf '%-9s %-34s %-4s exit %-3s %6ss  VUID %-3s %s\n' "$mode" "$name" "$verdict" "$code" "$seconds" "$vuids" "$last" | tee -a "$out"
    printf '%s\n' "$output" | sed 's/^/    /' >> "$out.full"
  done
done
echo "failures: $fails" | tee -a "$out"
