#!/bin/bash
set -e

echo "=== ksystemstats Ryzen Power Plugin — Build & Install ==="
echo ""

# 1. Dependencies
echo "[1/5] Checking dependencies..."
sudo pacman -S --needed cmake extra-cmake-modules qt6-base libksysguard

# 2. Build
echo ""
echo "[2/5] Building..."
mkdir -p build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)

# 3. Install plugin
echo ""
echo "[3/5] Installing plugin to /usr/lib/qt6/plugins/ksystemstats/..."
sudo cmake --install build

# 4. RAPL (powercap) permissions
echo ""
echo "[4/5] Setting RAPL energy counter read permissions..."
sudo cp 99-ryzen-powercap-readable.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=powercap 2>/dev/null || true

# 5. Restart ksystemstats
echo ""
echo "[5/5] Restarting ksystemstats..."
if systemctl --user restart plasma-ksystemstats 2>/dev/null; then
    echo "      restarted via systemd user service"
else
    pkill ksystemstats 2>/dev/null || true
    sleep 1
    /usr/bin/ksystemstats &
fi

echo ""
echo "=== Done! ==="
echo "Open KDE System Monitor and search for 'Package Power' under CPU Power (RAPL)."
echo "If sensors don't appear, log out and back in."
