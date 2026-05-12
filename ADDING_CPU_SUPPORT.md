# Adding Support for a New AMD CPU

This guide walks you through mapping the `ryzen_smu` pm_table offsets for a new AMD CPU family and adding it to the plugin.

---

## Prerequisites

- `ryzen_smu` kernel module loaded and `/sys/kernel/ryzen_smu_drv/pm_table` readable
- `python3` available
- The CPU under test should be running Arch Linux or similar with `stress` installed (`sudo pacman -S stress`)

---

## Step 1 — Check your CPU family

First confirm `ryzen_smu` recognises your chip:

```bash
cat /sys/kernel/ryzen_smu_drv/pm_table_version
cat /sys/kernel/ryzen_smu_drv/pm_table_size
```

Note both values. The pm_table version and size are different for each CPU family:

| CPU Family | Example chips | pm_table version | pm_table size |
|---|---|---|---|
| Cezanne | 5600H, 5800H | 400005 | ~936 bytes |
| Rembrandt | 6800H, 6900HX | 450004 | ~1476 bytes |
| Dragon Range | 8940HX, 8945HX | 400005 | 2256 bytes |
| Phoenix | 7840HS, 7940H | 450005 | ~2340 bytes |

Note: Dragon Range and Cezanne share the same version string (400005) but have completely different layouts and different sizes — **always check the size too**.

Also check your CPU family string:
```bash
sudo ryzenadj --info 2>&1 | grep "CPU Family"
```

---

## Step 2 — Capture idle and load snapshots

Run this script **at idle** (no heavy processes running, wait 30 seconds after boot):

```bash
python3 -c "
import struct
with open('/sys/kernel/ryzen_smu_drv/pm_table', 'rb') as f:
    data = f.read()
floats = struct.unpack_from(f'{len(data)//4}f', data)
print(f'Total floats: {len(floats)}')
for i, v in enumerate(floats):
    if 1 < v < 200 or 30 < v < 110 or 400 < v < 6000:
        print(f'[{i:03d}] {v:.4f}')
" > /tmp/pm_idle.txt
echo "Idle snapshot saved to /tmp/pm_idle.txt"
```

Then start a stress test in another terminal:
```bash
stress -c $(nproc)
```

Wait 30 seconds for power to stabilise, then run the load snapshot:
```bash
python3 -c "
import struct
with open('/sys/kernel/ryzen_smu_drv/pm_table', 'rb') as f:
    data = f.read()
floats = struct.unpack_from(f'{len(data)//4}f', data)
print(f'Total floats: {len(floats)}')
for i, v in enumerate(floats):
    if 1 < v < 200 or 30 < v < 110 or 400 < v < 6000:
        print(f'[{i:03d}] {v:.4f}')
" > /tmp/pm_load.txt
echo "Load snapshot saved to /tmp/pm_load.txt"

# Stop stress test
killall stress
```

---

## Step 3 — Compare idle vs load to identify indices

Run this comparison script to find indices that changed significantly:

```bash
python3 << 'EOF'
import re

def parse_snapshot(path):
    result = {}
    with open(path) as f:
        for line in f:
            m = re.match(r'\[(\d+)\]\s+([\d.]+)', line)
            if m:
                result[int(m.group(1))] = float(m.group(2))
    return result

idle = parse_snapshot('/tmp/pm_idle.txt')
load = parse_snapshot('/tmp/pm_load.txt')

all_keys = sorted(set(list(idle.keys()) + list(load.keys())))

print(f"{'Index':<8} {'Idle':>10} {'Load':>10} {'Delta':>10}  Likely meaning")
print('-' * 65)

for k in all_keys:
    i = idle.get(k, 0)
    l = load.get(k, 0)
    delta = l - i
    if abs(delta) < 2:
        continue

    note = ''
    if abs(delta) > 30 and 1 < l < 200:
        note = '*** LARGE CHANGE — likely power or temp'
    elif 50 < l < 110 and 50 < i < 110:
        note = 'likely temperature (C)'
    elif 1 < l < 200:
        note = 'likely power (W) or freq (MHz/100)'
    elif l > 400:
        note = 'likely frequency (MHz)'

    print(f'[{k:03d}]    {i:10.3f} {l:10.3f} {delta:+10.3f}  {note}')
EOF
```

---

## Step 4 — Identify specific indices

Look for these patterns in the comparison output:

### Power indices (W)
- **Rises sharply from ~5-15W at idle to 30-100W under load** = actual power draw
- **Stays constant regardless of load** = power limits set by ryzenadj/EC
- The index with the largest delta that stays below 200W is usually **Package Power**

### Temperature indices (°C)
- **Idle around 40-65°C, load around 70-95°C** = CPU temperatures
- The highest-value temp under load that matches what `sensors` reports for Tctl = **Tctl index**
- Indices in a consecutive block (e.g. 16 in a row) with similar values = **per-core temps**

### Verify against known tools

Cross-check your identified indices against `sensors` output:
```bash
# Run simultaneously with stress test
watch -n 1 'sensors | grep -E "Tctl|Tccd|PPT|fan"'
```

And against the pm_table in real time:
```bash
# Replace INDEX with your candidate index
watch -n 1 'python3 -c "
import struct
with open(\"/sys/kernel/ryzen_smu_drv/pm_table\", \"rb\") as f:
    data = f.read()
floats = struct.unpack_from(f\"{len(data)//4}f\", data)
print(f\"index[INDEX] = {floats[INDEX]:.2f}\")
"'
```

If the value tracks what `sensors` shows for Tctl, you have the right index.

---

## Step 5 — Update the plugin source

Once you have confirmed indices, open `ryzen_power_plugin.cpp` and update the constants at the top:

```cpp
// Replace these with your CPU's verified indices
static constexpr int IDX_PKG_POWER    = XX;  // CPU package power (W)
static constexpr int IDX_FAST_LIMIT   = XX;  // PPT fast limit (W)
static constexpr int IDX_FAST_ACTUAL  = XX;  // PPT fast actual (W)
static constexpr int IDX_SLOW_LIMIT   = XX;  // PPT slow limit (W)
static constexpr int IDX_SLOW_ACTUAL  = XX;  // PPT slow actual (W)
static constexpr int IDX_STAPM_LIMIT  = XX;  // STAPM limit (W)
static constexpr int IDX_STAPM_ACTUAL = XX;  // STAPM rolling average (W)
static constexpr int IDX_TCTL         = XX;  // CPU die temp (C)
static constexpr int IDX_HOTSPOT      = XX;  // Core hotspot temp (C)
static constexpr int IDX_CORE_START   = XX;  // First core temp (C)
static constexpr int NUM_CORES        = XX;  // Number of cores
```

Then rebuild and reinstall:
```bash
rm -rf build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
sudo cmake --install build
pkill ksystemstats
/usr/bin/ksystemstats &
```

---

## Step 6 — Contribute your findings

Please open a **GitHub issue or pull request** with:

1. Your CPU model and family string (from `ryzenadj --info`)
2. Your pm_table version and size
3. The confirmed index mapping table
4. Your kernel version and Plasma version

This helps future users with the same hardware get working sensors without having to repeat the mapping process.

**Template for your issue:**

```
CPU: AMD Ryzen X XXXXXX (Family: XXXX)
pm_table version: XXXXXX
pm_table size: XXXX bytes (XXX floats)
Kernel: X.X.X
Plasma: X.X.X

Verified index mapping:
| Index | Sensor | Idle value | Load value |
|-------|--------|-----------|-----------|
| XX    | Package Power (W) | X.X | X.X |
| XX    | PPT Fast Limit (W) | X.X | X.X |
...
```

---

## Known mappings

| CPU Family | Package Power | Tctl | Core temps start | Cores | Notes |
|---|---|---|---|---|---|
| Dragon Range (8940HX) | 20 | 11 | 330 | 16 | Verified on HP Omen 16, BIOS F.12 |
| *Your CPU here* | ? | ? | ? | ? | Please contribute! |
