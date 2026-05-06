#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-COM3}"
FQBN="${FQBN:-esp8266:esp8266:d1_mini}"
SKETCH_DIR="${SKETCH_DIR:-sketch_sep25a}"

usage() {
  cat <<'EOF'
Usage:
  ./arduino.sh compile      Compile sketch
  ./arduino.sh upload       Upload sketch
  ./arduino.sh deploy       Compile, then upload if compile succeeds
  ./arduino.sh both         Alias of deploy

Optional env vars:
  PORT=COMx
  FQBN=vendor:arch:board
  SKETCH_DIR=path/to/sketch

Examples:
  ./arduino.sh compile
  ./arduino.sh deploy
  PORT=COM4 ./arduino.sh upload
EOF
}

compile() {
  arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"
}

upload() {
  arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH_DIR"
}

cmd="${1:-help}"
case "$cmd" in
  compile)
    compile
    ;;
  upload)
    upload
    ;;
  deploy|both)
    compile
    upload
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    echo "Unknown command: $cmd" >&2
    echo >&2
    usage
    exit 1
    ;;
esac
