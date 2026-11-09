#!/bin/sh
dir="$(dirname "$0")"
args=""
for a; do args="$args|$a"; last="$a"; done
echo "$args" >> "$dir/grim.log"
case "$args" in
  *"|-T|dead|"*) echo "toplevel capture is not supported" >&2; exit 1 ;;
  *"|-T|51|"*) sleep 0.4 ;;
esac
printf 'JPEGDATA' > "$last"
