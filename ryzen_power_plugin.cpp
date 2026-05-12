#include "ryzen_power_plugin.h"

#include <QFile>
#include <QVariant>

#include <KPluginFactory>

K_PLUGIN_CLASS_WITH_JSON(RyzenPowerPlugin, "ryzen_power_plugin.json")

// Dragon Range 8940HX pm_table float indices
// Empirically mapped via idle/load correlation on HP Omen 16 (8940HX, BIOS F.12)
// pm_table size: 564 floats (2256 bytes), version: 400005
static constexpr int IDX_PKG_POWER    = 20;  // CPU package power (W) — best real-time metric
static constexpr int IDX_FAST_LIMIT   = 0;   // PPT fast limit (W)
static constexpr int IDX_FAST_ACTUAL  = 1;   // PPT fast actual (W)
static constexpr int IDX_SLOW_LIMIT   = 2;   // PPT slow limit (W)
static constexpr int IDX_SLOW_ACTUAL  = 3;   // PPT slow actual (W)
static constexpr int IDX_STAPM_LIMIT  = 6;   // STAPM limit (W)
static constexpr int IDX_STAPM_ACTUAL = 5;   // STAPM rolling average actual (W)
static constexpr int IDX_TCTL         = 11;  // CPU die temp Tctl (C)
static constexpr int IDX_HOTSPOT      = 69;  // Per-core hotspot temp (C)
static constexpr int IDX_CORE_START   = 330; // First of 16 core temps (C), indices 330-345
static constexpr int NUM_CORES        = 16;

RyzenPowerPlugin::RyzenPowerPlugin(QObject *parent, const QVariantList &args)
    : KSysGuard::SensorPlugin(parent, args)
{
    m_container = new KSysGuard::SensorContainer(
        QStringLiteral("ryzen_cpu"),
        QStringLiteral("CPU Power (Ryzen)"),
        this);

    auto *powerObj = new KSysGuard::SensorObject(
        QStringLiteral("power"), QStringLiteral("Power"), m_container);

    m_pkgPower    = makeSensor(powerObj, QStringLiteral("package"),        QStringLiteral("Package Power"),   KSysGuard::UnitWatt,    0, 150);
    m_fastActual  = makeSensor(powerObj, QStringLiteral("ppt_fast"),       QStringLiteral("PPT Fast Actual"), KSysGuard::UnitWatt,    0, 150);
    m_slowActual  = makeSensor(powerObj, QStringLiteral("ppt_slow"),       QStringLiteral("PPT Slow Actual"), KSysGuard::UnitWatt,    0, 150);
    m_stapmActual = makeSensor(powerObj, QStringLiteral("stapm"),          QStringLiteral("STAPM Actual"),    KSysGuard::UnitWatt,    0, 150);
    m_fastLimit   = makeSensor(powerObj, QStringLiteral("ppt_fast_limit"), QStringLiteral("PPT Fast Limit"),  KSysGuard::UnitWatt,    0, 150);
    m_slowLimit   = makeSensor(powerObj, QStringLiteral("ppt_slow_limit"), QStringLiteral("PPT Slow Limit"),  KSysGuard::UnitWatt,    0, 150);
    m_stapmLimit  = makeSensor(powerObj, QStringLiteral("stapm_limit"),    QStringLiteral("STAPM Limit"),     KSysGuard::UnitWatt,    0, 150);

    auto *tempObj = new KSysGuard::SensorObject(
        QStringLiteral("temperature"), QStringLiteral("Temperature"), m_container);

    m_tctl    = makeSensor(tempObj, QStringLiteral("tctl"),    QStringLiteral("Tctl (die)"),   KSysGuard::UnitCelsius, 0, 105);
    m_hotspot = makeSensor(tempObj, QStringLiteral("hotspot"), QStringLiteral("Core Hotspot"), KSysGuard::UnitCelsius, 0, 105);

    for (int i = 0; i < NUM_CORES; i++) {
        m_coreTemps.append(makeSensor(
            tempObj,
            QStringLiteral("core%1").arg(i),
            QStringLiteral("Core %1").arg(i),
            KSysGuard::UnitCelsius, 0, 105));
    }

    addContainer(m_container);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &RyzenPowerPlugin::updateValues);
    timer->setInterval(1000);
    timer->start();
}

QString RyzenPowerPlugin::providerName() const
{
    return QStringLiteral("ryzen_power");
}

void RyzenPowerPlugin::update()
{
    updateValues();
}

KSysGuard::SensorProperty *RyzenPowerPlugin::makeSensor(
    KSysGuard::SensorObject *obj, const QString &id, const QString &name,
    KSysGuard::Unit unit, qreal min, qreal max)
{
    auto *s = new KSysGuard::SensorProperty(id, name, 0.0, obj);
    s->setUnit(unit);
    s->setMin(min);
    s->setMax(max);
    s->setVariantType(QVariant::Double);
    return s;
}

float RyzenPowerPlugin::readFloat(const QByteArray &data, int index)
{
    const int offset = index * 4;
    if (offset + 4 > data.size()) return 0.0f;
    float val;
    memcpy(&val, data.constData() + offset, 4);
    return val;
}

void RyzenPowerPlugin::updateValues()
{
    QFile f(QStringLiteral("/sys/kernel/ryzen_smu_drv/pm_table"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray data = f.readAll();
    f.close();
    if (data.size() < (IDX_HOTSPOT + 1) * 4) return;

    m_pkgPower->setValue(static_cast<double>(readFloat(data, IDX_PKG_POWER)));
    m_fastActual->setValue(static_cast<double>(readFloat(data, IDX_FAST_ACTUAL)));
    m_slowActual->setValue(static_cast<double>(readFloat(data, IDX_SLOW_ACTUAL)));
    m_stapmActual->setValue(static_cast<double>(readFloat(data, IDX_STAPM_ACTUAL)));
    m_fastLimit->setValue(static_cast<double>(readFloat(data, IDX_FAST_LIMIT)));
    m_slowLimit->setValue(static_cast<double>(readFloat(data, IDX_SLOW_LIMIT)));
    m_stapmLimit->setValue(static_cast<double>(readFloat(data, IDX_STAPM_LIMIT)));
    m_tctl->setValue(static_cast<double>(readFloat(data, IDX_TCTL)));
    m_hotspot->setValue(static_cast<double>(readFloat(data, IDX_HOTSPOT)));

    for (int i = 0; i < NUM_CORES && i < m_coreTemps.size(); i++)
        m_coreTemps[i]->setValue(static_cast<double>(readFloat(data, IDX_CORE_START + i)));
}

#include "ryzen_power_plugin.moc"
