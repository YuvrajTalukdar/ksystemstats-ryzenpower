#include "rapl_power_reader.h"

#include <QDebug>

RaplPowerReader::RaplPowerReader(const QString &domainPrefix)
{
    // Find the RAPL domain whose name starts with domainPrefix.
    // e.g. "package" -> package-0 (intel-rapl:0), "core" -> core (intel-rapl:0:0)
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
}

double RaplPowerReader::update()
{
    if (m_path.isEmpty()) {
        return -1.0;
    }

    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1.0;
    }
    bool ok = false;
    const quint64 cur = QString::fromUtf8(f.readAll()).trimmed().toULongLong(&ok);
    if (!ok) {
        return -1.0;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!m_primed) {
        m_lastUj = cur;
        m_lastTime = now;
        m_primed = true;
        return -1.0; // first tick: no delta yet
    }

    // The counter wraps around max_energy_range_uj.
    const quint64 diff = (cur < m_lastUj)
        ? (m_hasMax ? (m_maxUj - m_lastUj) + cur : 0)
        : (cur - m_lastUj);

    const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - m_lastTime).count();
    m_lastUj = cur;
    m_lastTime = now;

    return dt > 0.0 ? static_cast<double>(diff) / (dt * 1e6) : -1.0; // uJ / (s * 1e6) = W
}
