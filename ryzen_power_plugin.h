#pragma once

#include <QString>
#include <QTimer>

#include <ksysguard/systemstats/SensorContainer.h>
#include <ksysguard/systemstats/SensorObject.h>
#include <ksysguard/systemstats/SensorPlugin.h>
#include <ksysguard/systemstats/SensorProperty.h>
#include <ksysguard/formatter/Unit.h>

#include "rapl_power_reader.h"

class RyzenPowerPlugin : public KSysGuard::SensorPlugin
{
    Q_OBJECT

public:
    explicit RyzenPowerPlugin(QObject *parent, const QVariantList &args);
    QString providerName() const override;
    void update() override;

private Q_SLOTS:
    void updateValues();

private:
    static KSysGuard::SensorProperty *makeSensor(
        KSysGuard::SensorObject *obj, const QString &id, const QString &name,
        KSysGuard::Unit unit, qreal min, qreal max);

    KSysGuard::SensorContainer *m_container = nullptr;
    RaplPowerReader *m_packageReader = nullptr;
    KSysGuard::SensorProperty *m_packagePower = nullptr;
};
