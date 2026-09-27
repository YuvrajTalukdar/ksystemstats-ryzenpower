#include "rapl_power_reader.h"

#include <QDebug>

namespace {
// Sub-sample period. The RAPL counter range on this platform is ~65.5 kJ, so
// the counter cannot wrap more than once between two sub-samples at any
// realistic power (one wrap per 100 ms would require ~655 kW).
constexpr int SubSampleIntervalMs = 100;
}

RaplPowerReader::RaplPowerReader(const QString &domainPrefix, QObject *parent)
    : QObject(parent)
{
    // Find the RAPL domain whose name starts with domainPrefix.
    // e.g. "package" -> package-0 (intel-rapl:0)
    const QDir powercap(QStringLiteral("/sys/class/powercap"));
    for (const QFileInfo &fi : powercap.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile nameFile(fi.absoluteFilePath() + QStringLiteral("/name"));
        if (!nameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
        if (!name.startsWith(domainPrefix)) {
            continue;
        }
        m_path = fi.absoluteFilePath() + QStringLiteral("/energy_uj");
        break;
    }

    if (m_path.isEmpty()) {
        qWarning() << "RaplPowerReader: no RAPL domain starting with" << domainPrefix
                   << "in /sys/class/powercap";
        return;
    }

    // Counter range, if exposed, for wrap-around handling.
    const QString maxPath = QFileInfo(m_path).absolutePath() + QStringLiteral("/max_energy_range_uj");
    QFile maxFile(maxPath);
    if (maxFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_maxUj = QString::fromUtf8(maxFile.readAll()).trimmed().toULongLong(&m_hasMax);
    }

    connect(&m_timer, &QTimer::timeout, this, &RaplPowerReader::sample);
    m_timer.start(SubSampleIntervalMs);
}

void RaplPowerReader::sample()
{
    if (m_path.isEmpty()) {
        return;
    }

    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    bool ok = false;
    const quint64 cur = QString::fromUtf8(f.readAll()).trimmed().toULongLong(&ok);
    if (!ok) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!m_primed) {
        m_lastUj = cur;
        m_lastSampleTime = now;
        m_primed = true;
        return;
    }

    // The counter wraps around max_energy_range_uj.
    const quint64 diff = (cur < m_lastUj)
        ? (m_hasMax ? (m_maxUj - m_lastUj) + cur : 0)
        : (cur - m_lastUj);

    m_accUj += static_cast<double>(diff);
    m_accSeconds += std::chrono::duration_cast<std::chrono::duration<double>>(now - m_lastSampleTime).count();
    m_lastUj = cur;
    m_lastSampleTime = now;
}

double RaplPowerReader::drain()
{
    if (m_accSeconds <= 0.0) {
        return -1.0;
    }
    const double w = m_accUj / (m_accSeconds * 1e6); // uJ / (s * 1e6) = W
    m_accUj = 0.0;
    m_accSeconds = 0.0;
    return w;
}
