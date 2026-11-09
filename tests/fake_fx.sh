#!/bin/sh
dir="$(dirname "$0")"
args=""
prev=""
for a; do
  flat=$(printf '%s' "$a" | sed ':a;N;$!ba;s/\n/\\n/g')
  args="$args|$flat"; last="$a"
  if [ "$prev" = "--image" ]; then
    if [ -f "$a" ]; then echo "exists:$a" >> "$dir/images"; else echo "missing:$a" >> "$dir/images"; fi
  fi
  prev="$a"
done
echo "$(pwd)$args" >> "$dir/log"
case "$1" in
  models) echo "[models] 3 available"; echo " - vendor/alpha-1"; echo " - vendor/beta-2"; echo " - gamma"; exit 0 ;;
esac
case "$last" in
  fail) echo "[notice] skill discovery warning" >&2; echo "boom: not signed in" >&2; exit 1 ;;
  quiet) exit 3 ;;
  slow) printf 'part'; sleep 0.1; exec sleep 30 ;;
  utf8) printf 'caf\303'; sleep 0.2; printf '\251 ok'; exit 0 ;;
  *) printf 'Hello'; sleep 0.1; printf ' wor'; sleep 0.1; printf 'ld'; exit 0 ;;
esac
