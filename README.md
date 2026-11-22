# SA2 VR (sa2mr)

A VR and mixed-reality mod of **Sonic Adventure 2** for the **Valve Steam Frame**. It is built on
[OpenSA2](https://github.com/kittycache9/OpenSA2), a GPL-3.0 static recompilation of the 2012 Steam PC
game; our fork is [jonnyczi/OpenSA2](https://github.com/jonnyczi/OpenSA2) (branch `sa2mr`), a submodule
here at `external/OpenSA2`.

- `BRAINSTORM.md`: what we're making ("Chao in Your Room", "Room Run", diorama and portal views).
- `BUILD_NOTES.md`: how it's built, and every deviation from upstream.
- `platforms/steam-frame/`: the Frame installer, the on-device builder, the Steam entry and the launch script.
- `roomrun/`: the Room Run course generator.

## Install on a Steam Frame

### What you need

- **Sonic Adventure 2 installed through Steam on the Frame.** You need your own copy (Steam app
  213610, the 2012 PC version). The installer checks its files against the known hashes.
- **About 25 GB of free space** for the first install. The build's temporary files are deleted when
  it finishes, leaving about 1.5 GB (the app, the toolchain and a build cache for quick updates).
- **The Frame on its charger, and about an hour** for the first install. The installer translates
  the game's code on the Frame itself. Later updates usually only relink, which takes a few minutes.
- An internet connection: the installer downloads the app (about 25 MB) and the open-source
  translation toolchain (about 70 MB).

### Steps

1. On the Frame, switch to **desktop mode**.
2. Open the [latest release](https://github.com/jonnyczi/sa2mr/releases) in the browser and download
   **`Install-SA2MR.desktop`**.
3. Open the downloaded file (in Dolphin, double-click it; if KDE asks, choose to run it). A terminal
   opens and shows the progress:
   - it downloads the app and checks its SHA-256 against the release's `SHA256SUMS`;
   - it puts the app in `~/Applications/sa2mr`;
   - it builds the game from your Steam copy in the background (a systemd user unit, `sa2mr-build`).
     Closing the terminal doesn't stop the build. Open the installer again to see its progress;
   - when the build is done, it adds **"Sonic Adventure 2 VR"** to your Steam library (Steam closes for
     a moment and starts again).
4. Go back to the headset view and start **Sonic Adventure 2 VR** from your library.

If a step fails, the terminal and a desktop notice show the log's path
(`~/.local/share/sa2mr/logs/`). Running the installer again resumes from the step that failed.

### Update and uninstall

- **Update:** open `Install-SA2MR.desktop` again (or download it from a newer release). It installs
  the newest release, keeps your saves and the game it already built, and rebuilds only what changed.
  If you already have the newest release, it says so.
- **Uninstall:** download **`Uninstall-SA2MR.desktop`** from the release and open it. It removes the
  Steam entry, `~/Applications/sa2mr` and the build data in `~/.local/share/sa2mr`. It keeps your saves
  and settings (`~/.local/share/sa2recomp`, `~/.config/sa2mr`).
  - To delete those too, uninstall from a terminal instead:
    `bash ~/Applications/sa2mr/frame/install.sh --uninstall --purge` (it asks first).
- Settings: lines in `~/.config/sa2mr/game.env.local` override the shipped VR settings by name (for
  example `SA2_FOV_SCALE=1.2`).

### What gets downloaded

None of these files hold any SEGA code or data:
- **The app** (`sa2mr-app-linux-aarch64.tar.gz`): our programs (the VR player, the launcher, the
  installer that checks your game files) and their open-source libraries. It also holds the public
  OpenSA2 source with its build tools, and the Frame scripts.
- **The toolchain** (`sa2mr-toolchain-aarch64.tar.zst`, its own `toolchain-*` prerelease, fetched
  by the builder): remill and LLVM built for aarch64, zig, and two Python packages. It is all open
  source, built in CI from pinned sources, with its licences inside.
- **Steamless 3.1.0.5,** fetched by the builder from its author's GitHub release and checked by
  SHA-256. It removes the Steam DRM wrapper from your copy of the executable, and runs under Proton
  on the Frame.

The playable game (`bin/sa2-game`) is made on your Frame, from your files, and never leaves it.

### Legal

This is an unofficial fan project, not affiliated with or endorsed by SEGA or Valve. Sonic
Adventure 2 is © SEGA. You need your own legally bought Steam copy. The project never ships the
game's code, data or anything translated from it: CI never has the game, and every upload is
scanned for game files. The code here and in the OpenSA2 fork is open source (OpenSA2: GPL-3.0); the
bundled third-party components keep their own licences (`share/licenses/`, `kit/licenses/`).
