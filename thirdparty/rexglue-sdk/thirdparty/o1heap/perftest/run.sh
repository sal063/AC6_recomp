#!/usr/bin/env bash
# Copyright (c) Pavel Kirienko

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$script_dir/build"
sdk_path=""
serial_port=""
mount_path=""
baud="115200"
no_flash=0
no_serial=0

usage() {
  cat <<USAGE
Usage: $0 [options]

Options:
  --sdk PATH        Pico SDK path (or set PICO_SDK_PATH)
  --port DEVICE     Serial device (e.g. /dev/ttyUSB0)
  --mount PATH      BOOTSEL mount path (e.g. /media/$USER/RP2350)
  --build-dir PATH  Build directory (default: perftest/build)
  --baud N          Serial baud (default: 115200)
  --no-flash        Build only; skip UF2 flashing
  --no-serial       Build/flash only; skip serial output
  -h, --help        Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --sdk)
      sdk_path="$2"
      shift 2
      ;;
    --port)
      serial_port="$2"
      shift 2
      ;;
    --mount)
      mount_path="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --baud)
      baud="$2"
      shift 2
      ;;
    --no-flash)
      no_flash=1
      shift
      ;;
    --no-serial)
      no_serial=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$sdk_path" ]]; then
  sdk_path="${PICO_SDK_PATH:-}"
fi
if [[ -z "$sdk_path" ]]; then
  echo "PICO_SDK_PATH is not set. Use --sdk or export PICO_SDK_PATH." >&2
  exit 1
fi

cmake -S "$script_dir" -B "$build_dir" -DPICO_SDK_PATH="$sdk_path" -DPICO_BOARD=pico2
cmake --build "$build_dir"

uf2_path="$build_dir/o1heap_perftest.uf2"
if [[ ! -f "$uf2_path" ]]; then
  echo "UF2 not found: $uf2_path" >&2
  exit 1
fi

detect_mount() {
  local base name candidate
  for base in "/media/$USER" "/run/media/$USER" "/media" "/run/media"; do
    for name in RP2350 RPI-RP2; do
      candidate="$base/$name"
      if [[ -d "$candidate" ]]; then
        echo "$candidate"
        return 0
      fi
    done
  done
  return 1
}

if [[ $no_flash -eq 0 ]]; then
  if [[ -z "$mount_path" ]]; then
    if ! mount_path="$(detect_mount)"; then
      echo "Waiting for BOOTSEL mount..." >&2
      for _ in $(seq 1 30); do
        sleep 1
        if mount_path="$(detect_mount)"; then
          break
        fi
      done
    fi
  fi

  if [[ -z "$mount_path" ]]; then
    echo "BOOTSEL mount not found. Put the board into BOOTSEL and retry." >&2
    exit 1
  fi

  echo "Flashing to $mount_path"
  cp "$uf2_path" "$mount_path/"
  sync
  sleep 1
fi

if [[ $no_serial -eq 1 ]]; then
  exit 0
fi

if [[ -z "$serial_port" ]]; then
  if [[ -d /dev/serial/by-id ]]; then
    for dev in /dev/serial/by-id/*; do
      if [[ -e "$dev" ]]; then
        serial_port="$dev"
        break
      fi
    done
  fi
fi

if [[ -z "$serial_port" ]]; then
  if [[ -e /dev/ttyUSB0 ]]; then
    serial_port="/dev/ttyUSB0"
  elif [[ -e /dev/ttyACM0 ]]; then
    serial_port="/dev/ttyACM0"
  fi
fi

if [[ -z "$serial_port" ]]; then
  echo "Serial port not found. Use --port to specify." >&2
  exit 1
fi

echo "Opening serial port $serial_port at ${baud} baud"
if command -v picocom >/dev/null 2>&1; then
  exec picocom -b "$baud" "$serial_port"
fi

echo "picocom not found; using stty+cat (Ctrl-C to exit)" >&2
stty -F "$serial_port" "$baud" raw -echo
trap 'stty -F "$serial_port" sane' EXIT
cat "$serial_port"
