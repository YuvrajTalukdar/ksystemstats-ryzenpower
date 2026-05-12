#pragma once

#include <QList>
#include <QTimer>

#include <ksysguard/systemstats/SensorContainer.h>
#include <ksysguard/systemstats/SensorObject.h>
#include <ksysguard/systemstats/SensorPlugin.h>
#include <ksysguard/systemstats/SensorProperty.h>
#include <ksysguard/formatter/Unit.h>

class RyzenPowerPlugin : public KSysGuard::SensorPlugin
{
    Q_OBJECT

public:
    RyzenPowerPlugin(QObject *parent, const QVariantList &args);
    QString providerName() const override;
    void update() override;

private Q_SLOTS:
    void updateValues();

private:
    static KSysGuard::SensorProperty *makeSensor(
        KSysGuard::SensorObject *obj, const QString &id, const QString &name,
        KSysGuard::Unit unit, qreal min, qreal max);
    static float readFloat(const QByteArray &data, int index);

    KSysGuard::SensorContainer *m_container   = nullptr;
    KSysGuard::SensorProperty  *m_pkgPower    = nullptr;
    KSysGuard::SensorProperty  *m_fastActual  = nullptr;
    KSysGuard::SensorProperty  *m_slowActual  = nullptr;
    KSysGuard::SensorProperty  *m_stapmActual = nullptr;
    KSysGuard::SensorProperty  *m_fastLimit   = nullptr;
    KSysGuard::SensorProperty  *m_slowLimit   = nullptr;
    KSysGuard::SensorProperty  *m_stapmLimit  = nullptr;
    KSysGuard::SensorProperty  *m_tctl        = nullptr;
    KSysGuard::SensorProperty  *m_hotspot     = nullptr;
    QList<KSysGuard::SensorProperty *> m_coreTemps;
};
