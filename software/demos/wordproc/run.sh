#!/bin/bash
# ZineWrite + xeyes on the bar: the word processor in the left 800 px (50x10 at Terminus 32 px,
# paper colours), xeyes top right following the text cursor (wp.py moves the pointer).
#   tools/sim/sim sdl software/demos/wordproc/run.sh
cd "$(dirname "$0")"
xsetroot -solid '#2b2b2b'
xeyes -geometry 150x116+805+4 &
eyes=$!
trap 'kill $eyes 2>/dev/null' EXIT
xterm -geometry 50x10+0+0 -fn '-*-terminus-medium-r-normal--32-*-*-*-c-160-iso10646-1' \
      -bg '#f4f1e8' -fg '#1d1d1d' -cr '#c0392b' -bw 0 -b 0 +sb -title ZineWrite \
      -e python3 wp.py
