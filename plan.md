# Plan — Accurate CPU Power in KDE System Monitor

*Research findings + implementation plan. Generated 2026-09-27 while investigating on the
target machine (LENOVO 83M0, Ryzen 7 260 / Radeon 780M, kernel `7.2.7-arch1-1`).*

**Status (2026-09-27): implemented and verified.** The plugin now reads RAPL (`RaplPowerReader`),
the Package Power sensor is live in KDE System Monitor, and `ryzenadj`/`ryzen_smu` were removed
from the system. Two deviations from the plan:

1. The RAPL *core* domain is not exposed, because on this platform it tracks only ~7 W of the ~80 W
   full-load package power.
2. §4.1/§4.2 specified computing watts from the delta between successive plugin ticks. In practice
   ksystemstats also calls a plugin's `update()` on its own ~1 s cycle, so two calls land
   milliseconds apart and a plain delta measures ~0 ms of energy → the sensor showed a 0/80 W
   sawtooth under load. Fixed by having `RaplPowerReader` sub-sample the counter internally every
   100 ms and accumulate the deltas; `drain()` reports the true window average regardless of call
   timing, and returns -1.0 ("keep previous value") when nothing was accumulated.

---

## 1. The problem

This repo is a KDE System Monitor (ksystemstats) plugin that exposes **CPU package power** and
temperatures for AMD Ryzen chips. It was originally written for an **HP Omen 16 (8940HX,
"Dragon Range")** and reads power from the `ryzen_smu` kernel module's
`/sys/kernel/ryzen_smu_drv/pm_table`.

Current symptoms on the machine this is being run on now:

1. The plugin's sensors (`PPT Fast Actual`, `Package Power`, …) **no longer work** in
   KDE System Monitor — they read nothing.
2. The **default "CPU power" sensor** in KDE System Monitor **always shows 0**.
3. `btop` and **Mission Control** *do* show accurate CPU power. The goal is to make this
   plugin do the same.

---

## 2. What the research found

### 2.1 This is NOT the machine the project was written for

| | Original target | This machine |
|---|---|---|
| DMI product | HP Omen 16 | **LENOVO 83M0** |
| CPU | Ryzen 9 8940HX (Dragon Range, chiplet) | **"Ryzen 7 260" / Radeon 780M** |
| CPU family | — (Dragon Range) | **family 25 (0x19)**, 16 logical CPUs |

Consequences:

- The `pm_table` float offsets in `ryzen_power_plugin.cpp` (`IDX_PKG_POWER=20`, etc.) were
  **empirically mapped for the 8940HX**. On this chip they are meaningless.
- The `ryzen_smu` module isn't even loaded here (see 2.2), so the plugin has nothing to read
  regardless of offsets.

### 2.2 Root cause: the plugin's data source is gone

- `/sys/kernel/ryzen_smu_drv/` **does not exist**; `modinfo ryzen_smu` → *"Module ryzen_smu not found"*.
- `dkms status` shows `ryzen_smu/187.0bb95d9` built **only for `6.18.54-1-lts`**, **not** for the
  running kernel `7.2.7-arch1-1`.
- **Cause:** the system was upgraded from the LTS kernel to the arch `7.2.7` kernel. The `ryzen_smu`
  DKMS module was never built for the new kernel, so it can't be `modprobe`d, so `pm_table` was never
  created.
- The plugin's `updateValues()` opens the file, fails, and silently `return`s — so every sensor stays
  at 0 / never updates. That is exactly the "sensor stopped working" symptom.

> This is the fragility the README warns about ("Surviving updates"): the approach depends on a
> hand-built out-of-tree kernel module that must be rebuilt for **every** kernel.

### 2.3 Why the built-in KDE "CPU power" shows 0

Inspected the **installed** ksystemstats 6.7.5 plugins (`strings` on the `.so` files) to see what each
actually reads:

| Plugin | Reads | CPU power? |
|---|---|---|
| `ksystemstats_plugin_cpu.so` | CPU **frequency** + **temperature** (via `coretemp` / `k10temp` hwmon) | **No** |
| `ksystemstats_plugin_power.so` | **Battery** only (`Solid::Battery`) | No |
| `ksystemstats_plugin_gpu.so` | **amdgpu PPT** (iGPU power) | No (iGPU, not CPU) |
| `ksystemstats_plugin_lmsensors.so` | whatever `sensors` prints (amdgpu PPT + temps) | No (iGPU PPT) |

- **No installed ksystemstats library references the RAPL path** (`/sys/class/powercap/...`).
- So this ksystemstats build has **no working CPU-power backend**. The "default CPU power" the user
  sees is either a sensor with no valid source (→ 0) or is actually the **amdgpu PPT** (iGPU-only,
  misleading — see 2.4).

### 2.4 The amdgpu PPT sensor is useless for CPU power (confirmed by measurement)

Live full-core load test (`stress -c 16`) on this machine:

| Source | Idle | Full load |
|---|---|---|
| **RAPL package** (`intel-rapl:0`) | **5.62 W** | **80.78 W** |
| amdgpu PPT (`power1_average`) | 5.67 W | **15.61 W** |
| k10temp Tctl | 37.6 °C | **91.8 °C** |

- **RAPL tracks the CPU** — power and temperature rise together (5.6 W / 38 °C → 80.8 W / 92 °C).
- **amdgpu PPT stalls at ~15 W** under full load — it is *not* CPU package power on this chip. This
  confirms the original project's core premise (the iGPU/PPT sensor doesn't reflect CPU workload).

### 2.5 How `btop` gets CPU power → **RAPL**

Source: `btop/src/linux/btop_collect.cpp:1032-1071`.

- `get_cpuConsumptionUJoules()` reads **`/sys/class/powercap/intel-rapl:0/energy_uj`** (RAPL *package*
  energy counter, in microjoules).
- `get_cpuConsumptionWatts()` keeps the previous counter value + timestamp (statics) and computes
  `watts = (current - previous) / (Δt)` (µJ / µs = W). First tick returns 0; `supports_watts =
  (usage > 0)`.

### 2.6 How Mission Control gets CPU power → **RAPL**

- The Rust GUI (`mission-center`) just renders `cpu.power_draw_w` provided by its **"magpie" backend**
  (the `gng` library, `subprojects/magpie` → `mission-center-devs/gng`).
- Source: `gng/platform-linux/src/cpu/power_draw.rs`:
  - Scans **`/sys/class/powercap`**, finds the domain whose `name` starts with **`package`**
    (e.g. `package-0`), reads its **`energy_uj`** and **`max_energy_range_uj`**.
  - `update()`: `watts = diff_uj / (elapsed_seconds × 1e6)`, with counter-**wraparound** handling via
    `max_energy_range_uj`.
- Its installer ships a udev rule to make the counter world-readable:
  `SUBSYSTEM=="powercap", KERNEL=="intel-rapl*", RUN+="/usr/bin/chmod a+r /sys/%p/energy_uj"`.

**Both reference tools use the same source: the RAPL `package` energy counter.** That is the answer to
"how do these tools get accurate CPU power" — and it is the approach we should adopt.

### 2.7 The enabler: RAPL is live and accurate on *this* machine

- `dmesg`: `intel_rapl_common: Found RAPL domain package` and `Found RAPL domain core`. Modules
  `intel_rapl_msr` + `intel_rapl_common` are loaded; kernel config has `CONFIG_INTEL_RAPL=m`.
- `/sys/class/powercap/intel-rapl:0` (`package-0`) and `intel-rapl:0:0` (`core`) exist, with
  `energy_uj` **readable as an unprivileged user** (already `a+r`).
- Measured real power: **5.6 W idle → 80.8 W full load**, tracking Tctl (see 2.4).
- **RAPL is kernel-native** (in-tree `intel_rapl` driver) — **no DKMS module**, so it survives reboots
  and kernel upgrades far more robustly than `ryzen_smu`.

> **Caveat:** RAPL availability is chip/kernel-dependent. This particular APU exposes it (which is why
> btop/Mission Control work here). It is **not guaranteed** on the original Dragon Range 8940HX — that
> is presumably why the Omen needed `ryzen_smu` in the first place. Verify RAPL presence on any other
> target chip before relying on it.

---

## 3. Answer: was `ryzenadj` / `ryzen_smu` the right way?

**No — not for this machine.**

- `ryzenadj` only works on AMD families it supports. Here it **fails to initialize**:
  `no compatible ryzen_smu kernel module found, fallback to /dev/mem` → `PCI Bus is not writeable` →
  `Unable to init ryzenadj`.
- The `ryzen_smu` `pm_table` path requires an out-of-tree DKMS module that must be rebuilt for every
  kernel, and its offsets are per-chip.
- **The right source here is RAPL** — the same one btop and Mission Control use, kernel-native, and
  already readable.

---

## 4. The plan (recommended)

**Re-point the plugin from the `ryzen_smu` `pm_table` to the RAPL `package` energy counter** — i.e.
implement exactly what btop and Mission Control do. Keep the existing ksystemstats plugin scaffolding
(`SensorContainer` / `SensorObject` / `SensorProperty` + 1 s `QTimer`); only the data source and the
sensors change.

### 4.1 Design

- **Data source:** `/sys/class/powercap/intel-rapl:0/energy_uj` (RAPL *package*). Add a **generic
  fallback** that scans `/sys/class/powercap` for a domain named `package*` (mirrors Mission Control),
  in case the numeric suffix differs. Optionally also read the `core` domain (`intel-rapl:0:0`).
- **Power computation:** on each 1 s tick, read `energy_uj` + a monotonic timestamp; compute
  `watts = (cur − prev) / Δt`. Handle counter **wraparound** using `max_energy_range_uj`. First tick
  primes the counter and reports no value.
- **Sensors to expose** (under the existing "CPU Power (Ryzen)" container):
  - **Package Power (W)** — the main, accurate CPU package power (RAPL `package`).
  - **Core Power (W)** — optional, from RAPL `core` domain.
  - *Temperatures:* already available via `k10temp` → the **lmsensors** plugin already shows `Tctl`.
    This chip's `k10temp` exposes **only Tctl** (no per-core / hotspot), so the old per-core/hotspot
    sensors are not reproducible from a standard source here. Drop them, or keep them only when a
    `pm_table` is actually present.
- **Robustness:** if the RAPL path is missing/unreadable, report *no data* (don't crash, don't
  pretend 0) and `qWarning()`. Permissions: `energy_uj` is already `a+r` here; ship a udev rule like
  Mission Control's for portability.
- **RAPL does not provide PPT/STAPM limits** (those are `ryzenadj`/SMU concepts). The new sensor set is
  therefore simpler (Package Power + Core Power). That's fine — the user wants **CPU power**, and this
  is the number that is actually correct.

### 4.2 Reference implementation (C++, drop-in for the plugin)

```cpp
#include <QFile> <QDir> <QFileInfo> <QDateTime>
#include <chrono>

struct RaplPowerReader {
    QString path;                       // .../energy_uj
    quint64 maxUj = 0; bool hasMax = false;
    quint64 lastUj = 0;
    std::chrono::steady_clock::time_point lastTime;
    bool primed = false;

    bool discover() {
        // Preferred: the standard path.
        if (QFile::exists(QStringLiteral("/sys/class/powercap/intel-rapl:0/energy_uj")))
            path = QStringLiteral("/sys/class/powercap/intel-rapl:0/energy_uj");
        else { // Generic fallback: first domain named "package*"
            const QDir pc(QStringLiteral("/sys/class/powercap"));
            for (const QString &d : pc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString np = pc.filePath(d) + QStringLiteral("/name");
                if (QFile::exists(np)) {
                    QFile f(np);
                    if (f.open(QIODevice::ReadOnly)) {
                        const QString name = QString::fromUtf8(f.readAll()).trimmed().toLower();
                        if (name.startsWith(QLatin1String("package"))) { path = pc.filePath(d) + QStringLiteral("/energy_uj"); break; }
                    }
                }
            }
        }
        if (path.isEmpty()) return false;
        const QString mp = QFileInfo(path).absolutePath() + QStringLiteral("/max_energy_range_uj");
        if (QFile::exists(mp)) { QFile f(mp); if (f.open(QIODevice::ReadOnly)) maxUj = QString::fromUtf8(f.readAll()).trimmed().toULongLong(&hasMax); }
        return true;
    }

    // Watts since last call, or -1 (not primed / unreadable).
    double update() {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return -1.0;
        bool ok = false;
        const quint64 cur = QString::fromUtf8(f.readAll()).trimmed().toULongLong(&ok);
        if (!ok) return -1.0;
        const auto now = std::chrono::steady_clock::now();
        if (!primed) { lastUj = cur; lastTime = now; primed = true; return -1.0; }
        const quint64 diff = (cur < lastUj) ? (hasMax ? (maxUj - lastUj) + cur : 0) : (cur - lastUj);
        const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - lastTime).count();
        lastUj = cur; lastTime = now;
        return dt > 0.0 ? (double)diff / (dt * 1e6) : -1.0;   // µJ / (s·1e6) = W
    }
};
```

In `RyzenPowerPlugin`:
- **Constructor:** create one `RaplPowerReader` per domain (package, core). If `discover()` fails,
  `qWarning()` and skip creating that sensor. Create `Package Power` (+ optional `Core Power`)
  `SensorProperty`(s) with `Unit::UnitWatt`.
- **`updateValues()` (1 s tick):** `double w = reader->update(); if (w >= 0) sensor->setValue(w);`

### 4.3 Implementation steps

1. **Edit `ryzen_power_plugin.{h,cpp}`** — replace the `pm_table`/`readFloat` logic with the
   `RaplPowerReader` above; expose `Package Power` (+ optional `Core Power`). Keep the container name so
   it still appears in the System Monitor sensor picker.
2. **Rebuild & install** (`./install.sh`, or the CMake steps in the README).
3. **Permissions** — confirm `energy_uj` is `a+r`; if not, install a udev rule
   (`SUBSYSTEM=="powercap", KERNEL=="intel-rapl*", RUN+="/usr/bin/chmod a+r /sys/%p/energy_uj"`).
4. **Test** (see §5).

### 4.4 Why this beats rebuilding `ryzen_smu`

- **Kernel-native** (in-tree `intel_rapl`) → no DKMS, no per-kernel rebuild, survives upgrades/reboots.
- **Same source as btop & Mission Control** → numbers match what you already trust.
- **No per-chip offset mapping** needed.
- `ryzen_smu` is fragile (out-of-tree, must rebuild for every kernel) and its offsets were for a
  *different* chip.

### 4.5 Fallback for chips *without* RAPL

If a target chip (e.g. the original Dragon Range 8940HX) does **not** expose RAPL, keep the `ryzen_smu`
`pm_table` path as a secondary source — but that requires the DKMS module built for the running kernel
*and* the correct per-chip offsets. The plugin can `#ifdef`/runtime-select: prefer RAPL, fall back to
`pm_table` if present.

---

## 5. Verification plan

1. Build + install the modified plugin; restart `ksystemstats`.
2. Open **KDE System Monitor** → confirm **Package Power (W)** appears under the CPU Power (Ryzen)
   container and is non-zero at idle.
3. **Cross-check vs btop** — run `btop`; its CPU power should match the plugin's within a small margin.
4. **Cross-check vs a manual RAPL delta** (ground truth):
   ```bash
   python3 -c "
   import time
   p='/sys/class/powercap/intel-rapl:0/energy_uj'
   e1=int(open(p).read()); t1=time.monotonic(); time.sleep(5)
   e2=int(open(p).read()); t2=time.monotonic()
   print(f'{(e2-e1)/1e6/(t2-t1):.2f} W over {t2-t1:.1f}s')"
   ```
5. **Load test** — `stress -c $(nproc)`: expect ~5 W idle → ~80 W load, tracking `k10temp` Tctl.
6. **Reboot** — confirm the sensor still works (RAPL is kernel-native; should just work).

---

## 6. Caveats / open items

- **RAPL is chip/kernel-dependent.** Verified on *this* Lenovo / "Ryzen 7 260". If you return to the
  Omen (Dragon Range 8940HX), check `ls /sys/class/powercap` first — if there's no `intel-rapl`/`package`
  domain there, the `ryzen_smu` path is still required (with the module rebuilt for the current kernel).
- **Temperatures:** this chip's `k10temp` exposes only `Tctl` (no per-core/hotspot). Those come from the
  lmsensors plugin already; the old per-core sensors can't be reproduced from a standard source here.
- **1 s sampling** yields a 1-second-average power (RAPL's counter is cumulative, so short bursts are
  smoothed). This matches btop/Mission Control behavior.
- **`PPT`/`STAPM` limits** are not available via RAPL; they were `ryzen_smu`/SMU-specific. The new sensor
  set is intentionally smaller (Package + Core power).

---

## 7. One-paragraph summary

Your plugin died because the out-of-tree `ryzen_smu` kernel module wasn't rebuilt for the new `7.2.7`
kernel, so its `pm_table` disappeared. The built-in KDE "CPU power" shows 0 because this ksystemstats
build has no working CPU-power backend (its CPU plugin only reads frequency + temperature; the only
power sensors are the battery and the iGPU's amdgpu PPT, which is useless for CPU load — measured at
just 15 W under full load vs 81 W real). **btop and Mission Control both read CPU power from the
kernel-native RAPL counter `/sys/class/powercap/intel-rapl:0/energy_uj`, computing watts from the
delta** — and that counter is live and accurate on this machine (5.6 W idle → 80.8 W load). The fix is
to re-point the plugin at RAPL the same way those tools do: no DKMS module, no per-chip offsets, robust
across kernel updates.
