# ProtonForge

**Advanced DLSS & Proton Manager for Steam Games on Linux**

ProtonForge is a powerful Qt6 application designed to give Linux gamers full control over NVIDIA DLSS settings and Proton versions for Steam games. Configure DLSS Super Resolution, Ray Reconstruction, Frame Generation, HDR support, and more - all from a single, intuitive interface.

> **Note:** ProtonForge currently focuses on NVIDIA graphics cards for DLSS-related features. Proton management features work with any GPU.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Qt Version](https://img.shields.io/badge/Qt-6.0%2B-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/Platform-Linux-blue.svg)](https://www.linux.org/)
[![GPU](https://img.shields.io/badge/GPU-NVIDIA_Focused-76B900.svg)](https://www.nvidia.com/)

## ✨ Features

### DLSS Configuration
- **Super Resolution (SR)**: Configure DLSS upscaling modes (Performance, Balanced, Quality, DLAA, Ultra Performance)
- **Ray Reconstruction (RR)**: Fine-tune ray tracing denoising
- **Frame Generation (FG)**: Control DLSS 3 multi-frame generation
- **Custom Scaling Ratios**: Set precise render resolution percentages
- **Render Presets**: Choose from preset A-O for optimal quality/performance balance

### HDR Support
- **Individual HDR Options**: Separately configure PROTON_ENABLE_WAYLAND, PROTON_ENABLE_HDR, and ENABLE_HDR_WSI
- **Smart Detection**: Automatic detection of system HDR status on KDE Plasma and Gnome
- **Desktop Environment Awareness**: Provides specific instructions for enabling HDR based on your DE
- **Wayland Check**: Warns if you're running on X11 (HDR requires Wayland)

### Proton Management
- **Automatic Updates**: Check for and install Proton-CachyOS and Proton-GE updates
- **Version Selection**: Choose specific Proton versions per game
- **Version Browser**: Browse and install any available Proton release
- **Smart Notifications**: Get notified about new versions, with intelligent "don't ask again" for dismissed updates
- **Delete Versions**: Remove unused Proton installations to save disk space

### Game Launch & Integration
- **Direct Launch**: Start games directly from ProtonForge with custom settings
- **Steam Integration**: Copy launch options to clipboard or write directly to Steam
- **Per-Game Settings**: Save unique configurations for each game
- **Executable Selection**: Automatically detect or manually choose the correct game executable
- **Launch Preview**: See exactly what environment variables will be set before launching

### Performance Tuning
- **Frame Rate Limiting**: Set precise FPS caps (DXVK_FRAME_RATE)
- **Smooth Motion**: Enable vsync enhancements for tear-free gaming
- **DLSS Upgrade**: Force newer DLSS DLL versions

### User Interface
- **Game Library Browser**: Beautiful grid view with Steam artwork
- **Real-time Preview**: See launch command changes in real-time
- **Native Linux Support**: Separate settings for native Linux games
- **Single Instance**: Prevents multiple app instances running simultaneously

## 📋 Requirements

### Runtime
- **Operating System**: Linux (tested on Ubuntu, Arch, Fedora)
- **Desktop Environment**: KDE Plasma 5.27+ or Gnome 46+ (for HDR support)
- **Display Server**: Wayland (required for HDR)
- **Qt**: Qt6 6.0 or later
- **Steam**: Installed and configured
- **NVIDIA GPU**: For DLSS features (GTX 16xx/RTX 20xx or newer)

### Build Dependencies
- CMake 3.16+
- C++17 compatible compiler (GCC 9+ or Clang 10+)
- Qt6 development packages:
  - Qt6Core
  - Qt6Widgets
  - Qt6Network
  - Qt6Concurrent

## 🚀 Installation

### From Flatpak (Recommended - Universal Linux)

Flatpak provides the easiest installation method that works across all Linux distributions:

```bash
# Install from GitHub releases
wget https://github.com/theinvisible/proton-forge/releases/download/v1.0.3/protonforge.flatpak
flatpak install protonforge.flatpak

# Run
flatpak run org.protonforge.ProtonForge
```

Or from Flathub (once published):

```bash
flatpak install flathub org.protonforge.ProtonForge
```

### From .deb Package (Debian/Ubuntu)

> **Built on:** Ubuntu 25.10 (Oracular Oriole) - the latest Ubuntu release. Compatible with Ubuntu 24.04 LTS and other Debian-based distributions with Qt6 6.0+.

```bash
# Download the latest release
wget https://github.com/theinvisible/proton-forge/releases/download/v1.0.3/protonforge_1.0.3_amd64.deb

# Install
sudo dpkg -i protonforge_1.0.3_amd64.deb

# Install dependencies if needed
sudo apt-get install -f
```

### From Source

```bash
# Clone the repository
git clone https://github.com/theinvisible/proton-forge.git
cd protonforge

# Create build directory
mkdir build && cd build

# Configure and build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

# Install (optional)
sudo cmake --install .
```

### Build .deb Package

```bash
# Build release version
mkdir cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
cd ..

# Create .deb package
bash build-deb.sh
```

## 🎮 Usage

### First Launch

1. **Start ProtonForge**:
   ```bash
   protonforge
   ```

2. **Select a Game**: Click on any Steam game in the library

3. **Configure Settings**:
   - Enable NVAPI (required for DLSS)
   - Configure DLSS options as needed
   - Set up HDR if you have compatible hardware
   - Choose Proton version

4. **Launch or Export**:
   - Click **Play** to launch directly with custom settings
   - Click **Copy to Clipboard** to get launch options
   - Click **Write to Steam** to save settings permanently

### Proton Management

**Install/Update Proton**:
- Go to **Tools** → **Install Proton-CachyOS**
- Select desired version from the list
- Wait for download and installation

**Check for Updates**:
- **Tools** → **Check for Proton Updates**
- Get notified about new releases

**Delete Old Versions**:
- Open version dialog
- Select installed version
- Click **Delete Selected**

### HDR Configuration

For HDR to work, you need:
1. HDR-capable display
2. Wayland session
3. HDR enabled in system settings

**KDE Plasma**:
1. System Settings → Display Configuration
2. Select your monitor
3. Enable "Allow HDR"

**Gnome**:
```bash
gsettings set org.gnome.mutter experimental-features "['hdr']"
```

## 🔧 Configuration Files

ProtonForge stores configurations in:

- **Settings**: `~/.config/ProtonForge/settings.json`
- **Image Cache**: `~/.cache/ProtonForge/images/`
- **Qt Settings**: Standard QSettings location

Settings are automatically saved per-game and persist across sessions.

## 🏗️ Project Structure

```
protonforge/
├── src/
│   ├── core/           # Game data, settings, DLSS configuration
│   ├── launchers/      # Steam launcher integration
│   ├── network/        # Image downloading and caching
│   ├── parsers/        # VDF parser for Steam configs
│   ├── runner/         # Game execution and Proton handling
│   ├── ui/             # Qt widgets and dialogs
│   └── utils/          # Utilities (EnvBuilder, ProtonManager, HDRChecker)
├── debian/             # Debian package configuration
├── CMakeLists.txt
└── build-deb.sh        # Automated .deb builder
```

## 🤝 Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

### Development Setup

```bash
# Clone and build debug version
git clone https://github.com/theinvisible/proton-forge.git
cd protonforge
mkdir cmake-build-debug && cd cmake-build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j$(nproc)

# Run from build directory
./ProtonForge
```

### Code Style
- C++17 standard
- Qt naming conventions
- Descriptive variable names
- Comments for complex logic

## 📝 License

ProtonForge is licensed under the MIT License. See [LICENSE](LICENSE) file for details.

```
Copyright (c) 2025 ProtonForge

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 🙏 Acknowledgments

- **NVIDIA**: For DLSS technology and NVAPI
- **Valve**: For Proton and Steam
- **CachyOS Team**: For Proton-CachyOS builds
- **GloriousEggroll**: For Proton-GE builds
- **Qt Project**: For the excellent Qt framework
- **dxvk-nvapi**: For making NVIDIA features work on Linux
- **Claude (Anthropic)**: This project was developed with AI assistance from Claude Code

## 🔗 Links

- **GitHub**: https://github.com/theinvisible/proton-forge
- **Issues**: https://github.com/theinvisible/proton-forge/issues
- **Releases**: https://github.com/theinvisible/proton-forge/releases

## ❓ FAQ

**Q: Do I need an NVIDIA GPU?**
A: Yes, DLSS features require NVIDIA RTX. However, you can still use ProtonForge for Proton management on any GPU.

**Q: Does this work with non-Steam games?**
A: Currently, ProtonForge focuses on Steam games. Support for other launchers may be added in the future.

**Q: Can I use this on X11?**
A: Yes, but HDR features require Wayland. All other features work on both X11 and Wayland.

**Q: Will this break my Steam installation?**
A: No, ProtonForge only modifies per-game launch options and installs Proton versions to the standard location (`~/.steam/root/compatibilitytools.d/`).

**Q: How do I report bugs?**
A: Please open an issue on GitHub with:
  - Your Linux distribution and version
  - Desktop environment
  - Steps to reproduce
  - Expected vs actual behavior

---

**Made with ❤️ for the Linux gaming community**
