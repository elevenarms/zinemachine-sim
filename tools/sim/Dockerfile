# Zine Machine display simulator: arm64 Debian with a 960x320 16-bit X display,
# streamed to the bezel-frame page over VNC (noVNC). See README.md.
FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
      xvfb x11vnc novnc websockify xterm xfonts-terminus x11-utils \
      build-essential pkg-config gdb libx11-dev libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
      inotify-tools procps psmisc python3 ca-certificates curl git less \
      gcc-i686-linux-gnu libc6-dev-i386-cross \
    && rm -rf /var/lib/apt/lists/*

COPY zmfb/ /opt/zm/src/zmfb/
RUN mkdir -p /opt/zm/lib /opt/zm/bin \
 && cc -O2 -Wall -fPIC -shared -o /opt/zm/lib/libzmfb.so /opt/zm/src/zmfb/zmfb_preload.c -ldl \
 && cc -O2 -Wall -o /opt/zm/bin/zmfb-run /opt/zm/src/zmfb/zmfb_run.c -lX11 -lutil

COPY bin/ /opt/zm/bin/
COPY www/ /opt/zm/www/
RUN chmod +x /opt/zm/bin/* && ln -s /usr/share/novnc /opt/zm/www/novnc \
 && ln -s /opt/zm/bin/zm /usr/local/bin/zm && ln -s /opt/zm/bin/zmfb-run /usr/local/bin/zmfb-run

ENV DISPLAY=:0 ZMFB_W=960 ZMFB_H=320 ZMFB_BPP=16 SDL_VIDEODRIVER=x11
WORKDIR /work
EXPOSE 6080
CMD ["/opt/zm/bin/zm-start"]
