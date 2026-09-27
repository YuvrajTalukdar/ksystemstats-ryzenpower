# ksystemstats-plugin-ryzen-power

A KDE System Monitor plugin that exposes **real CPU package power consumption** via the kernel's
**RAPL powercap interface** — the same data source used by **btop** and **Mission Control**.

---

## Background

### Why this plugin exists

KDE System Monitor ships **no working CPU power sensor** for AMD systems:

- The **amdgpu PPT** sensor (visible under GPU sensors) reports iGPU/IOD power only. Measured on
  this machine: ~6 W at idle and only ~15 W under full 8-core CPU load, while the CPU package is
  actually drawing ~80 W. Useless for CPU power monitoring.
- The CPU plugin in ksystemstats only exposes CPU *frequency* and *temperature*; the Power plugin
  is battery-only. There is no RAPL reader anywhere in ksystemstats.

**History:** this plugin was originally written for an HP Omen 16 (Ryzen 9 8940HX, "Dragon Range")
and read power from the `ryzen_smu` kernel module's `pm_table`. That approach required an out-of-tree
DKMS module that must be rebuilt for *every* kernel (the sensor silently died after a kernel upgrade)
and its `pm_table` float offsets were mapped for that one specific chip. On **2026-09-27** the plugin
was reworked to read **RAPL** instead: kernel-native, no DKMS, and the same source that btop and
Mission Control use. `ryzenadj` and `ryzen_smu` are no longer needed (and have been removed).

Research notes for that rework (how btop/Mission Control read power, live measurements, why the old
approach broke) are in [`plan.md`](plan.md).

### How it works

The kernel's in-tree `intel_rapl` driver exposes a **cumulative energy counter** (microjoules) at:

```
/sys/class/powercap/intel-rapl:0/energy_uj     (domain "package-0" = whole CPU package)
```

Power in watts is the delta of that counter divided by the elapsed time. This plugin reads the
counter once per second and publishes the result as a sensor — the exact same algorithm as
btop (`src/linux/btop_collect.cpp`, `get_cpuConsumptionWatts()`) and Mission Control's magpie backend
(`platform-linux/src/cpu/power_draw.rs`), including wrap-around handling via `max_energy_range_uj`.

Verified on this machine:

| | Idle | Full 8-core load (`stress -c 16`) |
|---|---|---|
| **RAPL package** | **5.6 W** | **80.8 W** |
| amdgpu PPT | 5.7 W | 15.6 W (wrong for CPU) |
| k10temp Tctl | 37.6 °C | 91.8 °C |

Notes:

- On an APU the RAPL *package* domain covers the **whole chip** (CPU + iGPU), which is the true
  package power figure.
- The RAPL *core* domain also exists on this platform, but it only tracks a small fraction of actual
  core power (~7 W under full 16-thread load vs ~80 W package), so it is **not** exposed as a sensor —
  it would be misleading.

---

## Requirements

- KDE Plasma 6.x with `ksystemstats` 6.x and `libksysguard` 6.x
- A Linux kernel whose in-tree `intel_rapl` driver exposes a RAPL **package** domain for your CPU.
  Arch's **mainline and LTS** kernels both ship `intel_rapl_common` + `intel_rapl_msr` in-tree
  (no DKMS involved), so kernel upgrades do not require any rebuild.
- No `ryzenadj`, no `ryzen_smu` module.

Tested on: **LENOVO 83M0**, "AMD Ryzen 7 260 w/ Radeon 780M" (CPU family 0x19),
kernel `7.2.7-arch1-1`, Plasma 6.7.5, ksystemstats 6.7.5.

**Check whether your machine has RAPL:**

```bash
ls /sys/class/powercap/                          # expect: intel-rapl, intel-rapl:0, ...
cat /sys/class/powercap/intel-rapl:0/name        # expect: package-0
```

RAPL exposure is **chip-dependent**: this APU presents an `intel-rapl`-compatible package domain.
Other chips may not (the original Dragon Range 8940HX did not — that's why the old `ryzen_smu`
approach existed). If `intel-rapl*` is missing after a kernel change, the plugin logs a warning and
registers no sensors (it does not fake a value).

---

## Installation

### Quick install

```bash
cd /path/to/ksystemstats-ryzenpower
./install.sh
```

### Manual install

**1. Build dependencies:**
```bash
sudo pacman -S cmake extra-cmake-modules qt6-base libksysguard
```

**2. Build:**
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
```

**3. Install:**
```bash
sudo cmake --install build
```

**4. RAPL read permissions** (same rule Mission Control ships; usually already readable, but this
makes it stick across reboots/kernel updates):
```bash
sudo cp 99-ryzen-powercap-readable.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=powercap
```

**5. Restart ksystemstats** (no reboot needed):
```bash
systemctl --user restart plasma-ksystemstats
```

---

## Usage

Open **KDE System Monitor** and search for **"Package Power"** in the sensor picker. It appears under
the **"CPU Power (RAPL)"** category:

- **Package Power (W)** — real-time CPU package power (1-second average). This is the number to use
  for CPU power monitoring; it matches what btop and Mission Control display.

**Sanity check** (ground truth, should match the sensor):
```bash
python3 -c "
import time
p = '/sys/class/powercap/intel-rapl:0/energy_uj'
e1 = int(open(p).read()); t1 = time.monotonic(); time.sleep(5)
e2 = int(open(p).read()); t2 = time.monotonic()
print(f'{(e2-e1)/1e6/(t2-t1):.2f} W over {t2-t1:.1f}s')"
```

---

## Surviving updates

- **Kernel updates:** nothing to do. The plugin is a userspace `.so` (no kernel coupling), and RAPL is
  in-tree (`intel_rapl_common`/`intel_rapl_msr`) in both Arch mainline and LTS kernels — no DKMS, no
  rebuild, no per-kernel module. The udev rule is kernel-agnostic.
  Quick sanity check after switching kernels: `ls /sys/class/powercap/` should still show
  `intel-rapl*`. If a sensor ever disappears, check
  `journalctl --user -u plasma-ksystemstats` for the plugin's warning.
- **ksystemstats / Plasma updates:** the plugin is compiled against `libksysguard`, so after a big
  Plasma update the sensor may stop appearing (ABI change). Then just rebuild and reinstall:
  ```bash
  rm -rf build
  cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build -j$(nproc)
  sudo cmake --install build
  systemctl --user restart plasma-ksystemstats
  ```
  To get reminded when ksystemstats updates, add a pacman hook:
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
  Exec = /usr/bin/echo "Rebuild ksystemstats-ryzenpower if Package Power sensor stops working"
  EOF
  ```

---

## Uninstallation

```bash
./uninstall.sh
```

This removes:
- `/usr/lib/qt6/plugins/ksystemstats/ksystemstats_plugin_ryzenpower.so` — the plugin itself
- `/etc/udev/rules.d/99-ryzen-powercap-readable.rules` — the udev rule for RAPL permissions

It does **not** remove the source folder or `build/` directory.

---

## License

LGPL-2.0-or-later
