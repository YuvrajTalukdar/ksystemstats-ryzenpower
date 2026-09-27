#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>

/**
 * Reads CPU package power (W) from a RAPL powercap domain.
 *
 * The kernel's intel_rapl driver exposes a cumulative energy counter
 * (microjoules) at /sys/class/powercap/<domain>/energy_uj. Power is the
 * delta of that counter divided by the elapsed time — the same source and
 * principle used by btop and Mission Control.
 *
 * The counter is sampled internally every 100 ms and the deltas are
 * accumulated. drain() returns the average power over the time accumulated
 * since the last drain() call, regardless of how often the caller invokes
 * it, and resets the accumulator.
 *
 * This decoupling is important: ksystemstats calls a plugin's update() on
 * its own ~1 s cycle in addition to the plugin's own 1 s timer. When two
 * calls land milliseconds apart, a plain "delta since last call" measures
 * ~0 ms of energy and reports ~0 W, which shows up as a 0/80 W sawtooth in
 * the graph under load. With internal sub-sampling, a drain() with nothing
 * new accumulated returns -1.0 ("no data") instead of a bogus 0.
 *
 * -1.0 means "no data" (not primed yet, file unreadable, or nothing
 * accumulated); callers should keep the previous sensor value.
 */
class RaplPowerReader : public QObject
{
    Q_OBJECT

public:
    explicit RaplPowerReader(const QString &domainPrefix, QObject *parent = nullptr);

    bool isAvailable() const { return !m_path.isEmpty(); }

    /**
     * Average power (W) over the time accumulated since the last drain, or
     * -1.0 if nothing was accumulated.
     */
    double drain();

private Q_SLOTS:
    void sample();

private:
    QString m_path; // .../energy_uj of the matched domain
    quint64 m_maxUj = 0;
    bool m_hasMax = false;
    quint64 m_lastUj = 0;
    bool m_primed = false;
    double m_accUj = 0.0; // energy accumulated since last drain (uJ)
    double m_accSeconds = 0.0; // time span of that accumulation (s)
    std::chrono::steady_clock::time_point m_lastSampleTime {};
    QTimer m_timer;
};
