# The SDL3 library hash depends on which optional dev packages the build host has

`build_host_deps.sh` configures SDL3 with a few options forced: ALSA on; PulseAudio, PipeWire, JACK, sndio, XScrnSaver and XTest off. Every other optional backend is auto-detected from the headers the build host happens to have.

On a clean Debian 13 container that otherwise matches `tools/pins.json`, `libSDL3.so.0` builds with sha256 `b3bd7ab6ba4b686db6550bbdd9b839763eab0da6265efd2b18bb88fb17259e9f`. The pin is `dcd594042e637353307b34f5876a66a3b86f86eccf5c7bf378893bf3b69f01ff`. The library embeds no absolute paths, so the difference is the feature set. In that container the configure summary had:

- **on:** wayland (dynamic), and x11 (dynamic) with xcursor, xdbe, xfixes, xinput2, xrandr, xshape and xsync;
- **off:** libdecor, D-Bus, IBus, libudev, libusb, liburing, KMSDRM, fribidi and libthai.

The `sdl3` pin's `use` text lists libdecor, D-Bus, IBus, libudev, KMSDRM, liburing and libusb as off, so the difference is probably in the X11 sub-options or in fribidi and libthai. Nothing records that, so a contributor cannot reproduce the pin or tell which define differs.

This affects more than reproducibility. The shipped SDL's backends decide window decorations under Wayland on GNOME (libdecor) and joystick hotplug and screensaver inhibit (udev, D-Bus).

## Proposal

- Pass every auto-detected option explicitly: `-DSDL_WAYLAND_LIBDECOR`, `-DSDL_DBUS`, `-DSDL_IBUS`, `-DSDL_LIBUDEV`, `-DSDL_HIDAPI_LIBUSB`, `-DSDL_LIBURING`, `-DSDL_KMSDRM`, `-DSDL_FRIBIDI`, `-DSDL_LIBTHAI`, and the X11 sub-options.
- Extend the existing CMakeCache check, which already checks the ALSA rows, so that it fails when a wanted value was not kept. That is the case when a required dev package is missing.
- Optionally pin the sha256 of `SDL_build_config.h` beside the library, so that a mismatch names the define that differs.

The values have to be yours, and the pin would move. Could you share the configure summary, or the dev package list, of the build host behind the current pin? With that I can send the PR, about 20 lines.
