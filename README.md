# ksystemstats-plugin-ryzen-power

A KDE System Monitor plugin that exposes **real CPU package power consumption** and **per-core temperatures** for AMD Ryzen Dragon Range processors (8940HX and similar) via the `ryzen_smu` kernel module's pm_table.

---

## Background

### Why this plugin exists

On AMD laptop CPUs, the obvious way to read CPU power in KDE System Monitor is via the iGPU's `PPT` sensor (reported by the `amdgpu` driver). This works correctly on **monolithic die** chips like the Ryzen 5 5600H (Cezanne) where the CPU cores and iGPU share a single die and a single power domain.

However, **Dragon Range HX chips** (8940HX, 8945HX, etc.) use a **chiplet design**:
- **CCD** (Core Complex Die) — contains the CPU cores, built on TSMC 4nm
- **IOD** (I/O Die) — contains the iGPU, memory controller, PCIe, built on TSMC 6nm

These are **physically separate dies**. The `amdgpu` PPT sensor only sees the IOD (~3-7W at idle), not the CCD where the actual CPU workload runs. Under full CPU load the amdgpu PPT stays near idle levels while the CPU cores consume 60W+ — making it useless for CPU power monitoring.

This plugin reads directly from the `ryzen_smu` kernel module's `pm_table` binary interface, which reports the true CPU package power from the SMU (System Management Unit) that monitors both dies.

### pm_table offset mapping

The Dragon Range pm_table layout is not publicly documented. The offsets used in this plugin were **empirically mapped** by correlating idle vs load values across all 564 float entries:

| Index | Sensor | Notes |
|-------|--------|-------|
| 20 | **CPU Package Power (W)** | Best real-time power metric |
| 0 | PPT Fast Limit (W) | Set by ryzenadj / EC |
| 1 | PPT Fast Actual (W) | Current burst power draw |
| 2 | PPT Slow Limit (W) | Set by ryzenadj / EC |
| 3 | PPT Slow Actual (W) | Medium-term power draw |
| 6 | STAPM Limit (W) | Long-term average limit |
| 5 | STAPM Actual (W) | Rolling average power draw |
| 11 | Tctl — CPU die temp (°C) | Main die temperature |
| 69 | Core hotspot (°C) | Highest per-core temp |
| 330–345 | Core 0–15 temps (°C) | Individual core temperatures |

Tested on: **HP Omen 16 (8940HX), BIOS F.12, kernel 7.0.3-arch1, Plasma 6.6.4**

---

## Requirements

- Arch Linux (or any distro with KDE Plasma 6)
- KDE Plasma 6.x
- `ksystemstats` 6.x
- `libksysguard` 6.x
- `ryzen_smu` kernel module loaded (provides `/sys/kernel/ryzen_smu_drv/pm_table`)
- AMD Ryzen Dragon Range CPU (8940HX, 8945HX, or similar HX-class chips)

### Install ryzen_smu

The `ryzen_smu` module is available from the AUR:
```bash
yay -S ryzen-smu-dkms
sudo modprobe ryzen_smu
# To load at boot:
echo "ryzen_smu" | sudo tee /etc/modules-load.d/ryzen_smu.conf
```

---

## Installation

### Quick install

```bash
git clone https://github.com/yourusername/ksystemstats-plugin-ryzen-power
cd ksystemstats-plugin-ryzen-power
./install.sh
```

### Manual install

**1. Install build dependencies:**
```bash
sudo pacman -S cmake extra-cmake-modules qt6-base libksysguard
```

**2. Build:**
```bash
mkdir -p build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
```

**3. Install:**
```bash
sudo cmake --install build
```

**4. Set pm_table permissions** (so ksystemstats can read it without root):
```bash
sudo cp 99-ryzen-smu-readable.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo chmod a+r /sys/kernel/ryzen_smu_drv/pm_table
```

**5. Restart ksystemstats:**
```bash
pkill ksystemstats
/usr/bin/ksystemstats &
```

---

## Usage

Open **KDE System Monitor** and search for **"Package Power"** in the sensor picker. The sensors appear under the **"CPU Power (Ryzen)"** category:

- **Package Power (W)** — real-time CPU package power draw. Use this for general monitoring.
- **PPT Fast Actual (W)** — power draw against the fast (burst) limit
- **PPT Slow Actual (W)** — power draw against the slow (sustained) limit
- **STAPM Actual (W)** — rolling time-average power (what the EC enforces long-term)
- **PPT Fast/Slow/STAPM Limit (W)** — the current limits set by ryzenadj or the EC
- **Tctl (°C)** — CPU die temperature
- **Core Hotspot (°C)** — highest individual core temperature
- **Core 0–15 (°C)** — per-core temperatures

**Which sensor to use for CPU power monitoring:**
Use **Package Power** — it's the instantaneous SMU reading of actual power consumption, updated every second. STAPM Actual is a smoothed rolling average useful for understanding throttling behaviour.

---

## Sensor explained: Package Power vs STAPM

| Sensor | What it measures | Update rate | Best for |
|--------|-----------------|-------------|----------|
| Package Power | Instantaneous draw right now | ~1s | Live monitoring, graphs |
| PPT Fast Actual | Draw vs burst limit | ~1s | Checking if burst headroom is used |
| PPT Slow Actual | Draw vs sustained limit | ~1s | Sustained workload monitoring |
| STAPM Actual | Rolling avg over ~2-5 min | ~1s | Understanding long-term throttling |

---

## Uninstallation

### Quick uninstall

```bash
./uninstall.sh
```

This removes:
- `/usr/lib/qt6/plugins/ksystemstats/ksystemstats_plugin_ryzenpower.so` — the plugin itself
- `/etc/udev/rules.d/99-ryzen-smu-readable.rules` — the udev rule for pm_table permissions

It does **not** remove the source folder or build directory. To remove everything:
```bash
./uninstall.sh
rm -rf /path/to/ksystemstats-plugin-ryzen-power
```

### Manual uninstall

```bash
sudo rm -f /usr/lib/qt6/plugins/ksystemstats/ksystemstats_plugin_ryzenpower.so
sudo rm -f /etc/udev/rules.d/99-ryzen-smu-readable.rules
sudo udevadm control --reload-rules
pkill ksystemstats
/usr/bin/ksystemstats &
```

---

## Surviving updates

The plugin is a compiled `.so` loaded by ksystemstats. After KDE/ksystemstats updates:

```bash
# Check if sensor still appears in System Monitor after an update
# If not, rebuild:
cd /path/to/ksystemstats-plugin-ryzen-power
rm -rf build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
sudo cmake --install build
pkill ksystemstats
```

To get reminded automatically when ksystemstats updates, add a pacman hook:
```bash
sudo tee /etc/pacman.d/hooks/ryzen-plugin-reminder.hook << 'EOF'
[Trigger]
Operation = Upgrade
Type = Package
Target = ksystemstats
Target = libksysguard

[Action]
Description = Ryzen power plugin may need rebuilding
When = PostTransaction
Exec = /usr/bin/echo "Rebuild ~/ksystemstats-plugin-ryzen-power if Package Power sensor stops working"
EOF
```

---

## Compatibility notes

- Tested on Dragon Range (8940HX). Other HX chips (8945HX, 7945HX) likely share the same pm_table layout but are untested — please open an issue if you verify compatibility.
- The `ryzenadj --info` command does **not** work on Dragon Range for reading values (returns an error about unsupported family). This plugin reads the pm_table directly, bypassing ryzenadj's monitoring interface entirely.
- The `amdgpu` PPT sensor visible in KDE System Monitor under GPU sensors reflects **IOD power only** (~3-7W) and is **not** a valid CPU power reading on Dragon Range.

---

## Contributing

If you have a different Dragon Range chip and want to verify/extend the pm_table offsets:

```bash
# Capture idle and load pm_table snapshots
python3 -c "
import struct
with open('/sys/kernel/ryzen_smu_drv/pm_table', 'rb') as f:
    data = f.read()
floats = struct.unpack_from(f'{len(data)//4}f', data)
for i, v in enumerate(floats):
    if 1 < v < 200 or 30 < v < 110:
        print(f'[{i:03d}] {v:.3f}')
"
```

Run at idle and under `stress -c $(nproc)` and compare — indices that change significantly between idle and load are the power/temp sensors.

---

## License

LGPL-2.0-or-later
