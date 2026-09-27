#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <chrono>

/**
 * Reads instantaneous power (W) from a RAPL powercap domain.
 *
 * The kernel's intel_rapl driver exposes a cumulative energy counter
 * (microjoules) at /sys/class/powercap/<domain>/energy_uj. Instantaneous
 * power is the delta of that counter divided by the time elapsed since the
 * previous update() call — the same approach used by btop
 * (src/linux/btop_collect.cpp, get_cpuConsumptionWatts()) and Mission
 * Control's magpie backend (platform-linux/src/cpu/power_draw.rs).
 *
 * The first update() call primes the counter and returns -1.0; subsequent
 * calls return the average power over the interval. update() also returns
 * -1.0 when the file cannot be read (e.g. the RAPL domain is gone).
 */
class RaplPowerReader
{
public:
    /**
     * Finds the RAPL domain under /sys/class/powercap whose name starts with
     * @p domainPrefix ("package" -> e.g. package-0, "core" -> core) and
     * prepares to read its energy counter.
     */
    explicit RaplPowerReader(const QString &domainPrefix);

    bool isAvailable() const { return !m_path.isEmpty(); }

    /**
     * Returns the power (W) since the last call, or -1.0 if the counter is
     * not primed yet or the file is unreadable.
     */
    double update();

private:
    QString m_path; // .../energy_uj of the matched domain
    quint64 m_maxUj = 0;
    bool m_hasMax = false;
    quint64 m_lastUj = 0;
    std::chrono::steady_clock::time_point m_lastTime {};
    bool m_primed = false;
};
