#!/bin/bash
set -e

echo "=== ksystemstats Ryzen Power Plugin — Uninstall ==="
echo ""

# 1. Remove the plugin
PLUGIN="/usr/lib/qt6/plugins/ksystemstats/ksystemstats_plugin_ryzenpower.so"
if [ -f "$PLUGIN" ]; then
    echo "[1/3] Removing plugin..."
    sudo rm -f "$PLUGIN"
    echo "      Removed $PLUGIN"
else
    echo "[1/3] Plugin not found at $PLUGIN — skipping."
fi

# 2. Remove udev rule
RULE="/etc/udev/rules.d/99-ryzen-powercap-readable.rules"
if [ -f "$RULE" ]; then
    echo "[2/3] Removing udev rule..."
    sudo rm -f "$RULE"
    sudo udevadm control --reload-rules
    echo "      Removed $RULE"
else
    echo "[2/3] Udev rule not found — skipping."
fi

# 3. Restart ksystemstats
echo "[3/3] Restarting ksystemstats..."
pkill ksystemstats 2>/dev/null || true
sleep 1
/usr/bin/ksystemstats &

echo ""
echo "=== Uninstall complete ==="
echo "The 'CPU Power (RAPL)' sensors will no longer appear in KDE System Monitor."
echo "The source folder and build directory have NOT been removed."
echo "To remove those too: rm -rf $(pwd)"
