#!/usr/bin/env bash

set -uo pipefail

PLATFORM=${PLATFORM:-}
GPU_DEVICE_NAME=${GPU_DEVICE_NAME:-}
XPU_DEVICE=${XPU_DEVICE:-}

for command_name in awk xpu-smi; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required command not found: $command_name" >&2
        exit 2
    fi
done

discovery_output=$(xpu-smi discovery)

if [[ -z "$XPU_DEVICE" ]]; then
    XPU_DEVICE=$(
        awk -F'|' -v device_name="$GPU_DEVICE_NAME" '
        index($0, "Device Name:") && (!device_name || index($0, device_name)) {
            device_id = $2
            gsub(/[[:space:]]/, "", device_id)
            print device_id
            exit
        }' <<< "$discovery_output"
    )
fi

if [[ -z "$XPU_DEVICE" ]]; then
    echo "GPU device not found by xpu-smi discovery${GPU_DEVICE_NAME:+: $GPU_DEVICE_NAME}" >&2
    exit 1
fi

if [[ -z "$GPU_DEVICE_NAME" ]]; then
    GPU_DEVICE_NAME=$(
        awk -F'|' -v device_id="$XPU_DEVICE" '
        index($0, "Device Name:") {
            id = $2
            gsub(/[[:space:]]/, "", id)
            if (id == device_id) {
                name = $3
                sub(/^[[:space:]]*Device Name:[[:space:]]*/, "", name)
                sub(/[[:space:]]*$/, "", name)
                print name
                exit
            }
        }' <<< "$discovery_output"
    )
fi

PLATFORM=${PLATFORM:-$GPU_DEVICE_NAME}
platform_width=${#PLATFORM}
if ((platform_width < 18)); then
    platform_width=18
fi

read_cpu_times() {
    awk '/^cpu / {
        idle = $5 + $6
        total = 0
        for (field = 2; field <= NF; field++) total += $field
        printf "%.0f %.0f\n", idle, total
    }' /proc/stat
}

read_system_memory_mib() {
    awk '
    /^MemTotal:/ { total = $2 }
    /^MemAvailable:/ { available = $2 }
    END { printf "%.2f", (total - available) / 1024 }
    ' /proc/meminfo
}

printf "Monitoring %s on xpu-smi device %s\n" "$GPU_DEVICE_NAME" "$XPU_DEVICE"
printf "%-*s %6s %10s %12s %10s %12s\n" "$platform_width" \
    "Platform" "CPU%" "Media Eng%" "Compute Eng%" "SysMem MiB" "VideoMem MiB"
printf '%*s %6s %10s %12s %10s %12s\n' "$platform_width" '' \
    "------" "----------" "------------" "----------" "------------" |
    tr ' ' '-'

read -r previous_idle previous_total <<< "$(read_cpu_times)"

while true; do
    sleep 1
    read -r current_idle current_total <<< "$(read_cpu_times)"
    cpu_percent=$(awk -v previous_idle="$previous_idle" -v previous_total="$previous_total" \
        -v current_idle="$current_idle" -v current_total="$current_total" '
        BEGIN {
            total_delta = current_total - previous_total
            idle_delta = current_idle - previous_idle
            cpu = 0
            if (total_delta > 0) {
                cpu = 100 * (total_delta - idle_delta) / total_delta
            }
            printf "%.2f", cpu
        }')
    previous_idle=$current_idle
    previous_total=$current_total
    system_memory=$(read_system_memory_mib)

    read -r media_percent compute_percent video_memory <<< "$(
        xpu-smi dump -d "$XPU_DEVICE" -m 33,31,18 -n 1 |
            awk -F, 'NR > 1 {printf "%.2f %.2f %.2f\n", $3, $4, $5; exit}'
    )"

    printf "%-*s %6s %10s %12s %10s %12s\n" "$platform_width" \
        "$PLATFORM" "$cpu_percent" "${media_percent:-0.00}" \
        "${compute_percent:-0.00}" "$system_memory" \
        "${video_memory:-0.00}"
done