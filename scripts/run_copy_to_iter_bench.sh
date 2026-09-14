#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
micros_dir="$repo_root/dsa-perf-micros"
micros="$micros_dir/src/dsa_perf_micros"
cpu=${CPU:-0}
iterations=${ITERATIONS:-10000}
repeats=${REPEATS:-5}

usage() {
    echo "Usage: $0 [--configure]"
    echo
    echo "Environment: CPU=0 ITERATIONS=10000 REPEATS=5"
    echo "--configure resets dsa0 and creates wq0.0 (dedicated, size 32, 4 engines)."
}

configure=0
case "${1:-}" in
    "") ;;
    --configure) configure=1 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

if [[ ! -x "$micros" ]]; then
    echo "Building dsa-perf-micros..."
    (
        cd "$micros_dir"
        ./autogen.sh
        ./configure CFLAGS='-g -O2' --prefix=/usr --sysconfdir=/etc --libdir=/usr/lib
        make -j"$(nproc)"
    )
fi

if (( configure )); then
    if (( EUID == 0 )); then
        "$micros_dir/scripts/setup_dsa.sh" -d dsa0 -w1 -q0 -m d -s32 -e4
    else
        sudo "$micros_dir/scripts/setup_dsa.sh" -d dsa0 -w1 -q0 -m d -s32 -e4
    fi
fi

sysfs=/sys/bus/dsa/devices
[[ -d "$sysfs/dsa0" ]] || { echo "dsa0 is absent" >&2; exit 1; }
[[ $(<"$sysfs/dsa0/state") == enabled ]] || {
    echo "dsa0 is disabled; rerun with --configure" >&2
    exit 1
}
[[ $(<"$sysfs/wq0.0/state") == enabled ]] || {
    echo "wq0.0 is disabled; rerun with --configure" >&2
    exit 1
}
[[ $(<"$sysfs/wq0.0/mode") == dedicated ]] || {
    echo "wq0.0 is not dedicated" >&2
    exit 1
}
[[ $(<"$sysfs/wq0.0/size") == 32 ]] || {
    echo "wq0.0 size is not 32" >&2
    exit 1
}
for engine in 0 1 2 3; do
    [[ $(<"$sysfs/engine0.$engine/group_id") == 0 ]] || {
        echo "engine0.$engine is not assigned to group 0" >&2
        exit 1
    }
done

run_dsa() {
    local qd=$1
    "$micros" \
        -n"$qd" -s64k -q"$qd" -w0 -o3 -i"$iterations" \
        -K"[$cpu]@dsa0,0" -c -f -zF,F -v0
}

echo "DSA 64 KiB MEMMOVE queue-depth sweep (CPU $cpu, $iterations iterations)"
for qd in 1 2 4 8 16 32; do
    printf 'QD=%-2s  ' "$qd"
    run_dsa "$qd" 2>&1 | grep 'GB per sec'
done

echo "QD=32 repeatability ($repeats runs)"
for ((run = 1; run <= repeats; run++)); do
    printf 'run=%-2s ' "$run"
    run_dsa 32 2>&1 | grep 'GB per sec'
done

echo "QD=32 without destination cache-control flag"
"$micros" \
    -n32 -s64k -q32 -w0 -o3 -i"$iterations" \
    -K"[$cpu]@dsa0,0" -c -zF,F -v0 2>&1 | grep 'GB per sec'
