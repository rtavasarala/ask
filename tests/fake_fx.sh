#!/bin/sh
dir="$(dirname "$0")"
args=""
for a; do args="$args|$a"; last="$a"; done
echo "$(pwd)$args" >> "$dir/log"
case "$last" in
  fail) echo "[notice] skill discovery warning" >&2; echo "boom: not signed in" >&2; exit 1 ;;
  quiet) exit 3 ;;
  slow) printf 'part'; sleep 0.1; exec sleep 30 ;;
  utf8) printf 'caf\303'; sleep 0.2; printf '\251 ok'; exit 0 ;;
  *) printf 'Hello'; sleep 0.1; printf ' wor'; sleep 0.1; printf 'ld'; exit 0 ;;
esac
