#!/bin/bash

set -e

echo "Building .deb package for ProtonForge..."

# Extract version from CMakeLists.txt (single source of truth)
VERSION=$(grep -oP 'project\(ProtonForge VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)
if [ -z "$VERSION" ]; then
    echo "Error: Could not extract version from CMakeLists.txt"
    exit 1
fi
echo "Version: ${VERSION}"

# Variables
PACKAGE_NAME="protonforge"
ARCH="amd64"
BUILD_DIR="debian-build"
PACKAGE_DIR="${BUILD_DIR}/${PACKAGE_NAME}_${VERSION}_${ARCH}"

# Clean previous build
rm -rf "${BUILD_DIR}"

# Create directory structure
mkdir -p "${PACKAGE_DIR}/DEBIAN"
mkdir -p "${PACKAGE_DIR}/usr/bin"
mkdir -p "${PACKAGE_DIR}/usr/share/applications"
mkdir -p "${PACKAGE_DIR}/usr/share/doc/${PACKAGE_NAME}"

# Copy and process the control file (replace @VERSION@ placeholder)
sed "s/@VERSION@/${VERSION}/g" debian/control > "${PACKAGE_DIR}/DEBIAN/control"

# Copy the binary
echo "Copying binary..."
cp cmake-build-release/ProtonForge "${PACKAGE_DIR}/usr/bin/protonforge"
chmod 755 "${PACKAGE_DIR}/usr/bin/protonforge"

# Create desktop entry
echo "Creating desktop entry..."
cat > "${PACKAGE_DIR}/usr/share/applications/${PACKAGE_NAME}.desktop" << 'EOF'
[Desktop Entry]
Name=ProtonForge
Comment=DLSS & Proton Manager for Steam Games
Exec=protonforge
Icon=protonforge
Terminal=false
Type=Application
Categories=Game;Utility;
Keywords=dlss;steam;proton;gaming;forge;
EOF

# Create copyright file
echo "Creating copyright file..."
cat > "${PACKAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/copyright" << 'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: protonforge
Source: https://github.com/theinvisible/proton-forge

Files: *
Copyright: 2025 ProtonForge
License: MIT
 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:
 .
 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.
 .
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 DEALINGS IN THE SOFTWARE.
EOF

# Create changelog
echo "Creating changelog..."
cat > "${PACKAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/changelog" << EOF
protonforge (${VERSION}) stable; urgency=medium

  * Initial release
  * DLSS Super Resolution, Ray Reconstruction, and Frame Generation support
  * HDR support configuration (Wayland)
  * Direct game launch with custom settings
  * Proton-CachyOS and Proton-GE management
  * Per-game settings persistence
  * Frame rate limiting for smooth motion
  * Steam CDN game artwork integration

 -- ProtonForge <noreply@protonforge>  $(date -R)
EOF

# Compress changelog
gzip -9 "${PACKAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/changelog"

# Calculate installed size (in KB)
INSTALLED_SIZE=$(du -sk "${PACKAGE_DIR}" | cut -f1)
echo "Installed-Size: ${INSTALLED_SIZE}" >> "${PACKAGE_DIR}/DEBIAN/control"

# Build the package
echo "Building .deb package..."
dpkg-deb --build --root-owner-group "${PACKAGE_DIR}"

# Move to current directory
mv "${PACKAGE_DIR}.deb" .

echo ""
echo "✓ Package built successfully: ${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
echo ""
echo "To install:"
echo "  sudo dpkg -i ${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
echo "  sudo apt-get install -f  # If dependencies are missing"
