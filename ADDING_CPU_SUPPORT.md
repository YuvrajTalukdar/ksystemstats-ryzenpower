# Adding Support for a New AMD CPU

Since the 2026-09-27 rework, the plugin reads CPU package power from the kernel-native
**RAPL** powercap interface (`/sys/class/powercap/<domain>/energy_uj`) — the same source btop and
Mission Control use. There are **no per-chip offsets or constants to map anymore**: the plugin
scans `/sys/class/powercap` at startup and uses whichever domain is named `package*`.

**If your chip exposes a RAPL `package` domain, the plugin works as-is — no source changes needed.**
This guide is what to check and verify.

> The pre-2026-09-27 version of this file (step-by-step `ryzen_smu` pm_table offset mapping) is
> still in the git history: `git show 8fc32e6:ADDING_CPU_SUPPORT.md`.

---

## Step 1 — Does your chip expose RAPL?

```bash
ls /sys/class/powercap/
# and show what each domain is named:
for d in /sys/class/powercap/*; do
  [ -f "$d/name" ] && printf '%-16s %s\n' "$(basename "$d")" "$(cat "$d/name")"
done
```

You want a domain whose name starts with `package`, e.g.:

```
intel-rapl        intel-rapl:0    package-0
intel-rapl:0:0    core
```

- **`package-0` present** → good, go to Step 2. The plugin picks it up automatically (it matches on
  the domain *name*, not the device name, so a different `intel-rapl:N` numbering still works).
- **No `package*` domain** → the plugin has no data source on this chip: it logs a warning
  (`journalctl --user -u plasma-ksystemstats`) and registers no sensors. See Step 4.

Also check readability (the plugin runs unprivileged):

```bash
sudo -u nobody cat /sys/class/powercap/intel-rapl:0/energy_uj && echo readable
```

If not readable, install the udev rule from the repo:

```bash
sudo cp 99-ryzen-powercap-readable.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=powercap
```

> Note: that rule only matches `KERNEL=="intel-rapl*"`. If your chip exposes RAPL under a different
> device name, the rule needs adjusting (same pattern, different `KERNEL==`).

---

## Step 2 — Build, install, sanity-check

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
sudo cmake --install build
systemctl --user restart plasma-ksystemstats
```

Then confirm **Package Power** appears in KDE System Monitor (under "CPU Power (RAPL)") and is
non-zero at idle.

**Ground truth** — manual RAPL delta, should match the sensor:

```bash
python3 -c "
import time
p = '/sys/class/powercap/intel-rapl:0/energy_uj'   # adjust domain if yours differs
e1 = int(open(p).read()); t1 = time.monotonic(); time.sleep(5)
e2 = int(open(p).read()); t2 = time.monotonic()
print(f'{(e2-e1)/1e6/(t2-t1):.2f} W over {t2-t1:.1f}s')"
```

**Load test:**

```bash
stress -c $(nproc) &    # wait ~30 s, watch the sensor, then: kill $!
```

Expected: idle watts (usually single digits) rising smoothly to full-load watts under `stress`,
and tracking `k10temp` Tctl from `sensors`. Cross-check against btop's CPU power or Mission
Control — they read the same counter, so the numbers should agree within a few percent.

Two things that are *not* bugs:

- The reading is a ~1 s average (RAPL's counter is cumulative), so short bursts are smoothed.
- On APUs the RAPL *package* domain covers the whole chip (CPU + iGPU) — that is the correct
  package figure. Some platforms also expose a RAPL *core* domain that tracks only a fraction of
  real core power (on the Lenovo 83M0: ~7 W of ~80 W full-load); the plugin deliberately does not
  expose it.

---

## Step 3 — If a `package` domain exists but the numbers look wrong

- Compare against the manual RAPL delta (Step 2) and btop. If those agree with each other but not
  with `sensors`/fan behaviour, the RAPL domain on this chip may not cover the full package —
  report it (Step 5).
- Check for counter wraparound: the counter wraps at `max_energy_range_uj` (a few hundred kJ); the
  plugin handles the wrap, so this only matters if the domain reports an implausibly large
  `max_energy_range_uj`.
- Check `journalctl --user -u plasma-ksystemstats` for the plugin's warnings.

---

## Step 4 — If your chip has no RAPL at all

RAPL exposure is **chip-dependent**. This plugin cannot invent a data source; it will register no
sensors rather than fake a value.

Options:

1. **Wait for kernel support.** In-tree RAPL support for AMD parts has been expanding; newer kernels
   may expose a `package` domain where older ones did not. Re-check Step 1 after a kernel update.
2. **Revive the old `ryzen_smu` pm_table path.** The original approach (out-of-tree DKMS module +
   per-chip float-offset mapping, full procedure in `git show 8fc32e6:ADDING_CPU_SUPPORT.md`) is
   still available in this repo's history. It works, but it must be rebuilt for *every* kernel and
   its offsets are per-chip — exactly the fragility this rework removed. Only do this if the chip
   genuinely has no other package-power source.
3. **Use another tool** (btop/Mission Control) — they have the same RAPL dependency and will not
   have the data either.

---

## Step 5 — Contribute your findings

Open a GitHub issue (or PR) so future users with the same hardware know what to expect. Include:

1. CPU model and family (e.g. `lscpu | grep -E 'Model name|CPU family'`)
2. Kernel version, Plasma version
3. Output of the Step 1 scan (does a `package*` domain exist? what's its name?)
4. Measured idle and full-load watts from the sensor, and from btop for cross-check
5. Anything unexpected (wrong numbers, unreadable counter, no domain)

**Template:**

```
CPU: AMD Ryzen X XXXXXX (Family: 0xXX)
Machine: XXXXXX
Kernel: X.X.X
Plasma: X.X.X

RAPL: present / absent
  Domain: intel-rapl:0 -> package-0        (output of Step 1 scan)
Sensor:  X.X W idle, XX.X W under `stress -c $(nproc)`
btop:    X.X W idle, XX.X W under load
Notes:  ...
```

---

## Known chips

| CPU (family) | Machine | RAPL `package` domain | Measured | Notes |
|---|---|---|---|---|
| Ryzen 7 260 w/ Radeon 780M (0x19) | Lenovo 83M0 | yes — `intel-rapl:0` → `package-0` | 5.6 W idle → 80.8 W load | Verified 2026-09-27; matches btop. RAPL *core* domain present but tracks only ~7 W of full-load package power, so it is not exposed |
| Ryzen 9 8940HX (Dragon Range) | HP Omen 16 | believed absent (unverified) | — | The original target chip; the old `ryzen_smu` approach existed because this chip had no RAPL source |
