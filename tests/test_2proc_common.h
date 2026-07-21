// Shared providers for the 2-process reproduction. Models basecamp's real
// QML-only topology: the modules (echo + capability) live in a SEPARATE process
// from the LogosQmlBridge, connected over QtRO local sockets — the one factor no
// in-process harness could model (separate event loops across the boundary).
#pragma once

#include "logos_provider_interface.h"
#include "module_proxy.h"

#include <QJsonArray>
#include <QString>
#include <QVariant>
#include <QVariantList>

namespace twoproc {

// Echo target: returns args.first() for "echo". Optionally emits an event on
// every call (LOGOS_FIRE_EVENTS) to race event delivery against the call chain.
class EchoProvider : public LogosProviderObject {
public:
    EventCallback emitFn;
    bool fireEvents = false;
    QVariant callMethod(const QString& method, const QVariantList& args) override {
        if (method == QLatin1String("echo") && !args.isEmpty()) {
            if (fireEvents && emitFn)
                emitFn(QStringLiteral("ev0"), QVariantList() << args.first());
            return args.first();
        }
        return QVariant();
    }
    bool informModuleToken(const QString&, const QString&) override { return true; }
    QJsonArray getMethods() override { return QJsonArray{}; }
    void setEventListener(EventCallback cb) override { emitFn = std::move(cb); }
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("echo_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }
};

// capability_module: requestModule(origin, target) mints a token and informs the
// target proxy (same process), so the target authorizes the caller afterwards.
class CapabilityProvider : public LogosProviderObject {
public:
    ModuleProxy* echoProxy = nullptr;
    bool rotate = false;
    int calls = 0;
    QVariant callMethod(const QString& method, const QVariantList& args) override {
        if (method == QLatin1String("requestModule") && args.size() >= 2) {
            const QString origin = args[0].toString();
            const QString mint = rotate
                ? QStringLiteral("echo-token-%1").arg(++calls)
                : QStringLiteral("echo-token");
            if (!rotate) ++calls;
            if (echoProxy) echoProxy->saveToken(origin, mint);
            return mint;
        }
        return QVariant();
    }
    bool informModuleToken(const QString&, const QString&) override { return true; }
    QJsonArray getMethods() override { return QJsonArray{}; }
    void setEventListener(EventCallback) override {}
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("capability_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }
};

} // namespace twoproc
