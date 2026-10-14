# ProtonForge

**DLSS, HDR and Proton manager for Linux gamers**

ProtonForge configures NVIDIA DLSS, HDR, Proton tweaks and launch options per
game, then launches the game directly or writes the options into Steam. It
handles your local **Steam** libraries and your **GOG** account side by side.
There is no GOG client for Linux, so ProtonForge installs GOG games itself.
Steam does not have to be installed for that.

![ProtonForge main window](https://hadler.me/wordpress/wp-content/uploads/2026/02/Bildschirmfoto_20260222_080736.png "ProtonForge main page")

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Qt Version](https://img.shields.io/badge/Qt-6.4%2B-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/Platform-Linux-blue.svg)](https://www.linux.org/)
[![GPU](https://img.shields.io/badge/GPU-NVIDIA_Focused-76B900.svg)](https://www.nvidia.com/)

> The DLSS features need an NVIDIA GPU. Proton management, HDR, GOG and the
> launcher work with any GPU.

## ✨ Features

**DLSS**
- Super Resolution, Ray Reconstruction and Frame Generation overrides, with a
  render preset for each (SR: transformer J/K, DLSS 4.5 L/M, legacy CNN; RR: D/E/F;
  FG: A/B and the beta Performance Mode C) or "Recommended" to let the driver pick
- Multi-Frame Generation up to 6x, Dynamic Multi-Frame Generation (RTX 50), and a
  33–100 % scaling ratio slider for mode CUSTOM
- Reflex, Smooth Motion, DLSS DLL upgrade (optionally pinned to a version),
  D3D12 shader extensions
- **Compatibility warnings** checked against your GPU, NVIDIA driver and the Proton
  the game would really start with. They warn, they never block. On a native Linux
  game, options only Proton would read are greyed out and explained

**HDR**
- `PROTON_ENABLE_WAYLAND`, `PROTON_ENABLE_HDR`/`DXVK_HDR` and `ENABLE_HDR_WSI` individually
- Detects the HDR state on KDE Plasma and GNOME and tells you how to turn it on

**Proton**
- Install, update and delete Proton-CachyOS and Proton-GE; choose a version per
  game or "Automatic" (Steam's own choice, else the newest installed)
- NTSync, controller input (without Steam Input / via SDL), frame rate limit, debug log
- Optional GitHub token in Settings raises the API rate limit from 60 to 5,000 requests/hour

**Games and launching**
- One library for Steam and GOG, with cover art, source badges and a filter
- Launch directly, copy the launch options, or write them to Steam (**Write to Launcher**).
  Nothing is written to Steam unless you click that button
- Live preview of the exact environment, executable selection, ProtonDB ratings
  and launch-option recommendations from ProtonDB reports
- Steam overlay toggle, MangoHud with a built-in configuration editor and live preview

**GOG**
- Sign in through your normal browser; ProtonForge never sees your password
- Parallel, verified downloads with pause and resume (also after quitting);
  updates fetch only the files that changed
- Native Linux builds are preferred; everything else runs under Proton
- Uninstall removes only what ProtonForge installed. Heroic and Lutris libraries are left alone
- Credentials (GOG, Steam Web API key, GitHub token) go to the system keyring,
  otherwise to a `0600` file

## 🚀 Installation

All packages are on the [releases page](https://github.com/theinvisible/proton-forge/releases/).

**Flatpak** (any distribution):

```bash
flatpak install protonforge.flatpak
flatpak run org.protonforge.ProtonForge
```

The sandbox reaches `~`, `/mnt`, `/media` and `/run/media`. For a Steam library
anywhere else, ProtonForge names the missing path and this command:
`flatpak override --user --filesystem=/path/to/SteamLibrary org.protonforge.ProtonForge`.
Flatpak Steam is detected automatically. Details are in [FLATPAK.md](FLATPAK.md).

**AppImage** (any distribution with glibc 2.36 or newer):

```bash
chmod +x ProtonForge-*-x86_64.AppImage
./ProtonForge-*-x86_64.AppImage
```

On Ubuntu 24.04 and newer, install `libfuse2t64` first, or start the file with
`--appimage-extract-and-run`.

**.deb** (Ubuntu LTS): pick the package for your release. A package for the
wrong release installs fine but does not start.

| Package | For |
|---|---|
| `protonforge_X.Y.Z~noble_amd64.deb` | Ubuntu 24.04 and derivatives |
| `protonforge_X.Y.Z~resolute_amd64.deb` | Ubuntu 26.04 and newer |

```bash
sudo apt-get install ./protonforge_*_amd64.deb
```

**From source** (CMake 3.16+, GCC 9+/Clang 10+, Qt 6.4+ with Core, Widgets,
Network, Concurrent and DBus, zlib; QtKeychain optional). The full list is
[`packaging/build-depends.txt`](packaging/build-depends.txt):

```bash
git clone https://github.com/theinvisible/proton-forge.git
cd proton-forge
grep -vE '^\s*(#|$)' packaging/build-depends.txt | xargs sudo apt-get install -y
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build      # optional
```

Packages: `bash build-deb.sh` and `bash build-appimage.sh . dist` (the AppImage
always builds in a Docker container).

## 🎮 Usage

1. Select a game, configure DLSS, HDR and Proton on the right.
2. Click **Play**, **Copy to Clipboard** or **Write to Launcher** (Steam games only;
   close Steam first).

**GOG:** Library → Game Stores… → GOG → **Sign in**. Log in in your browser,
then copy the address of the blank page GOG lands on back into ProtonForge.
Pick a game and click **Install**. Install location and language are under
Settings → GOG (default `~/Games/ProtonForge/`).

**Proton:** Tools → Proton-Manager to install or delete versions,
Tools → Check for Proton-CachyOS Updates for updates.

**HDR** needs an HDR display, a Wayland session and HDR enabled in the desktop:
KDE System Settings → Display Configuration → *Allow HDR*, or on GNOME
`gsettings set org.gnome.mutter experimental-features "['hdr']"`.

**Command line**, for scripts and bug reports:

```bash
protonforge --list-games                 # every game as JSON
protonforge --launch <id> --dry-run      # what would run, writing nothing
protonforge --gog-status                 # signed in? which credential store?
protonforge --store-list GOG             # owned games, installed or not
protonforge --gog-plan <productid>       # what an install would fetch, writing nothing
protonforge --gog-install <productid>
protonforge --gog-uninstall <productid>
```

## 🔧 Files

| Path | What |
|---|---|
| `~/.config/ProtonForge/settings.json` | per-game and default profiles |
| `~/.config/ProtonForge/ProtonForge.conf` | install location, language, UI state |
| `~/.config/ProtonForge/gog-installs.json` | what ProtonForge installed from GOG |
| `~/.config/ProtonForge/secrets.json` | credentials, only without a system keyring (`0600`) |
| `~/Games/ProtonForge/` | GOG games and their Proton prefixes |
| `~/.cache/ProtonForge/` | cover art, ProtonDB and GOG caches, safe to delete |

## ❓ FAQ

**Do I need Steam?** No. With only a GOG account, games install and launch, and
ProtonForge downloads Proton itself.

**Will it break my Steam installation?** No. It changes per-game launch options
only when you click **Write to Launcher**, and installs Proton into the standard
`compatibilitytools.d`. GOG games never get a Steam identity or the Steam overlay.

**X11?** Everything works except HDR, which needs Wayland.

**Is ProtonForge affiliated with GOG?** No. It uses the same interface as the
GOG Galaxy client.

**Found a bug?** [Open an issue](https://github.com/theinvisible/proton-forge/issues)
with your distribution, desktop, GPU and driver, and the output of
`protonforge --launch <id> --dry-run` if it is about a game.

## 🤝 Contributing

Pull requests and issues are welcome. [CLAUDE.md](CLAUDE.md) describes the
architecture, [TESTS.md](TESTS.md) the unit and integration tests,
[RELEASE.md](RELEASE.md) the release process, and [CHANGELOG.md](CHANGELOG.md)
what changed.

## 🙏 Acknowledgments

NVIDIA (DLSS, NVAPI) · Valve (Proton, Steam) · CachyOS (Proton-CachyOS) ·
GloriousEggroll (Proton-GE) · [dxvk-nvapi](https://github.com/jp7677/dxvk-nvapi) ·
the Qt Project. Developed with AI assistance from Claude Code.

## 📝 License

MIT. See [LICENSE](LICENSE).
