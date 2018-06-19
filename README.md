# libretro-mpv

mpv media player as a libretro core. A proof of concept release is now
available.

Aims to use features already established in mpv that are not currently
available in Retroarch movieplayer.

I want to be able to use Retroarch as my movie player on my embedded devices
(Raspberry Pi) and desktop using hardware acceleration without having to use
Kodi or mpv directly. Thus allowing for a more integrated experience, and
smaller root filesystem.

## Compiling

### Overview

Retroarch must be compiled with `--disable-ffmpeg` to stop the integrated
movieplayer from playing the input file.

FFmpeg (preferably master branch) must be compiled with `--enable-shared`.

mpv must be compiled with `--enable-libmpv-shared`.

Then run `make` in the mpv-libretro folder.

### Compiling with Mingw-w64 on Windows

RetroArch must be compiled with `--disable-ffmpeg` and have OpenGL or
OpenGLES enabled. Compiling RetroArch is not described here.

1. Open `Minwg-w64 64 bit` (not MSYS2 shell).
2. Install ffmpeg using `pacman -S mingw64/mingw-w64-x86_64-ffmpeg`.
3. Clone https://github.com/deltabeard/mpv.git using
`git clone https://github.com/deltabeard/mpv.git`. This fork has the audio
callback patches required for libretro-mpv.
4. Change to the new mpv directory.
6. Run ./bootstrap.py to download the waf build tool than mpv uses.
5. Configure mpv using `./waf configure --enable-libmpv-shared --prefix=/mingw64/`.
6. Compile and install mpv using `./waf install`.
7. Download libretro-mpv release 0.3.alpha by either checking out the
   `audio-cb-new` branch of `https://github.com/libretro/libretro-mpv.git` or
   by downloading
   https://github.com/libretro/libretro-mpv/archive/0.3.alpha.tar.gz .
8. Run `make` in the libretro-mpv folder. If using OpenGLES, run `make
   platform=gles` instead.

Overall, the dependencies required to build libretro-mpv are:
- ffmpeg 4.0 (provided by mingw64/mingw-w64-x86_64-ffmpeg)
- mpv from https://github.com/deltabeard/mpv.git fork.
