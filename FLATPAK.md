# Flatpak Build and Publishing Guide

This guide explains how to build, test, and publish ProtonForge as a Flatpak package.

## Prerequisites

Install Flatpak and flatpak-builder:

```bash
# Ubuntu/Debian
sudo apt install flatpak flatpak-builder

# Arch Linux
sudo pacman -S flatpak flatpak-builder

# Fedora
sudo dnf install flatpak flatpak-builder
```

Add Flathub repository:

```bash
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```

Install KDE runtime and SDK:

```bash
flatpak install flathub org.kde.Platform//6.8
flatpak install flathub org.kde.Sdk//6.8
```

## Building Locally

### 1. Update the SHA256 Hash

Before building, you need to update the SHA256 hash in `org.protonforge.ProtonForge.yml`:

```bash
# Download the source archive
wget https://github.com/theinvisible/proton-forge/archive/v1.0.3.tar.gz

# Calculate SHA256
sha256sum v1.0.3.tar.gz

# Update the sha256 field in org.protonforge.ProtonForge.yml
```

### 2. Build the Flatpak

```bash
# Create a build directory
mkdir -p flatpak-build

# Build the Flatpak
flatpak-builder --force-clean flatpak-build org.protonforge.ProtonForge.yml
```

### 3. Test the Flatpak

```bash
# Install locally for testing
flatpak-builder --user --install --force-clean flatpak-build org.protonforge.ProtonForge.yml

# Run the application
flatpak run org.protonforge.ProtonForge
```

### 4. Export and Create Bundle

```bash
# Export to a repository
flatpak-builder --repo=flatpak-repo --force-clean flatpak-build org.protonforge.ProtonForge.yml

# Create a single-file bundle for distribution
flatpak build-bundle flatpak-repo protonforge.flatpak org.protonforge.ProtonForge
```

## Publishing to Flathub

To publish on Flathub, you need to:

1. **Fork the Flathub repository template**:
   ```bash
   # Visit https://github.com/flathub/flathub and click "New app"
   # Or manually fork https://github.com/flathub/org.example.App
   ```

2. **Create a new repository** named `org.protonforge.ProtonForge` in the Flathub organization

3. **Submit the manifest**:
   - Copy `org.protonforge.ProtonForge.yml`, `.desktop`, and `.metainfo.xml` files
   - Add an icon file (required for Flathub)
   - Create a pull request to the Flathub repository

4. **Flathub Review Process**:
   - Flathub maintainers will review your submission
   - They will check the manifest, metadata, and icon
   - Once approved, the app will be available on Flathub

For detailed instructions, see: https://docs.flathub.org/docs/for-app-authors/submission/

## GitHub Actions Automation

To automate Flatpak builds in CI/CD, add the following to your GitHub Actions workflow:

```yaml
- name: Build Flatpak
  run: |
    sudo apt-get install -y flatpak flatpak-builder
    flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
    flatpak install -y flathub org.kde.Platform//6.8 org.kde.Sdk//6.8

    # Update SHA256 in manifest
    VERSION="${GITHUB_REF#refs/tags/v}"
    wget "https://github.com/theinvisible/proton-forge/archive/v${VERSION}.tar.gz"
    SHA256=$(sha256sum "v${VERSION}.tar.gz" | awk '{print $1}')
    sed -i "s/sha256: REPLACE_WITH_ACTUAL_SHA256/sha256: ${SHA256}/" org.protonforge.ProtonForge.yml

    # Build and bundle
    flatpak-builder --repo=flatpak-repo --force-clean flatpak-build org.protonforge.ProtonForge.yml
    flatpak build-bundle flatpak-repo protonforge.flatpak org.protonforge.ProtonForge

- name: Upload Flatpak Bundle
  uses: softprops/action-gh-release@v1
  with:
    files: protonforge.flatpak
```

## Installing from Bundle

Users can install the `.flatpak` bundle file directly:

```bash
# Download from GitHub releases
wget https://github.com/theinvisible/proton-forge/releases/download/v1.0.3/protonforge.flatpak

# Install
flatpak install protonforge.flatpak

# Run
flatpak run org.protonforge.ProtonForge
```

## Troubleshooting

### Build fails with missing dependencies

Make sure you have the KDE Platform and SDK installed for the correct version:

```bash
flatpak list | grep org.kde
```

### Permission issues

The Flatpak needs access to:
- Home directory (to access Steam library)
- `~/.steam` and `~/.local/share/Steam` (Steam directories)
- Network (for downloading Proton versions)
- GPU (for gaming)

These are defined in the `finish-args` section of the manifest.

### Testing Steam integration

Since Flatpak sandboxes the application, test that Steam directories are accessible:

```bash
flatpak run --command=sh org.protonforge.ProtonForge
# Inside the sandbox:
ls ~/.steam
ls ~/.local/share/Steam
```

## Icon Requirements

For Flathub submission, you need:
- An SVG icon (preferred) or PNG icons in multiple sizes
- Icon should be named `org.protonforge.ProtonForge.svg`
- Place in `icons/` directory and update manifest accordingly

## Notes

- The Flatpak uses KDE Platform 6.8 runtime for Qt6 support
- Update the runtime version when newer versions are available
- Always test locally before publishing
- Keep the metainfo.xml releases section updated with each version
