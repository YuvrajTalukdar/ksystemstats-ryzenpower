#include "ryzen_power_plugin.h"

#include <QVariant>

#include <KPluginFactory>

#include <QDebug>

K_PLUGIN_CLASS_WITH_JSON(RyzenPowerPlugin, "ryzen_power_plugin.json")

RyzenPowerPlugin::RyzenPowerPlugin(QObject *parent, const QVariantList &args)
    : KSysGuard::SensorPlugin(parent, args)
{
    m_container = new KSysGuard::SensorContainer(
        QStringLiteral("ryzen_cpu"),
        QStringLiteral("CPU Power (RAPL)"),
        this);

    auto *powerObj = new KSysGuard::SensorObject(
        QStringLiteral("power"), QStringLiteral("Power"), m_container);

    // RAPL "package" domain = total CPU package power. This is the number
    // btop and Mission Control display for CPU power.
    //
    // Note: the RAPL "core" domain exists on this platform too, but it only
    // tracks a small fraction of actual core power (measured ~7 W under full
    // 16-thread load vs ~80 W package), so it is not exposed as a sensor;
    // it would be misleading.
    m_packageReader = new RaplPowerReader(QStringLiteral("package"), this);
    if (m_packageReader->isAvailable()) {
        m_packagePower = makeSensor(powerObj, QStringLiteral("package"),
                                    QStringLiteral("Package Power"),
                                    KSysGuard::UnitWatt, 0, 250);
    }

    if (!m_packagePower) {
        qWarning() << "RyzenPowerPlugin: RAPL is not available on this system - no sensors registered";
        return; // container is a child of this and is cleaned up with the plugin
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

void RyzenPowerPlugin::updateValues()
{
    if (m_packagePower) {
        // drain() returns the average power accumulated since the last
        // drain, no matter who (our timer or ksystemstats' update cycle)
        // triggered this call; -1.0 means nothing was accumulated, in which
        // case the previous value is kept.
        const double w = m_packageReader->drain();
        if (w >= 0.0) {
            m_packagePower->setValue(w);
        }
    }
}

#include "ryzen_power_plugin.moc"
