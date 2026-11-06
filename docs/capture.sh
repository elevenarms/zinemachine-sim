#!/bin/bash
# Record what the simulated display shows as an animated GIF (for the README).
#   docs/capture.sh OUT.gif SECONDS [FPS] [WIDTH]   records whatever is running now
# (WIDTH scales the GIF down, e.g. 640 for busy full-screen effects; default 960)
# Frames are grabbed inside the container with xwd (pixel-exact 960x320, RGB565) and
# turned into a GIF on the host with ffmpeg (palettegen/paletteuse).
set -eu
out=$1 secs=$2 fps=${3:-10} width=${4:-960}
NAME=${ZM_SIM_NAME:-zm-sim}
cap=/tmp/zmcap
docker exec "$NAME" bash -c "rm -rf $cap && mkdir -p $cap"
# grab; print 'frames seconds' so the GIF plays back at the real speed
read -r n el < <(docker exec "$NAME" bash -c '
  cap='$cap' secs='$secs' fps='$fps'
  t0=$(date +%s.%N); i=0
  while :; do
    now=$(date +%s.%N); el=$(echo "$now - $t0" | bc)
    [ "$(echo "$el >= $secs" | bc)" = 1 ] && break
    xwd -root -silent > $cap/$(printf %05d $i).xwd; i=$((i+1))
    nxt=$(echo "$i / $fps" | bc -l); d=$(echo "$nxt - $el" | bc -l)
    [ "$(echo "$d > 0" | bc)" = 1 ] && sleep "$d"
  done
  echo $i $(echo "$(date +%s.%N) - $t0" | bc)')
tmp=$(mktemp -d)
docker cp -q "$NAME:$cap/." "$tmp/"
rate=$(echo "$n / $el" | bc -l)
ffmpeg -loglevel error -y -framerate "$rate" -i "$tmp/%05d.xwd" \
  -vf "scale=$width:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle" \
  -loop 0 "$out"
rm -rf "$tmp"
printf '%s: %d frames, %.1f s, %.1f fps, %s\n' "$out" "$n" "$el" "$rate" "$(du -h "$out" | cut -f1)"
