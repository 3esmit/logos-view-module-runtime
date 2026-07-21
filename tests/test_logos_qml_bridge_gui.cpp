// GUI reproduction: run the QML-only plugin pattern inside a real QQuickView
// scene on a QGuiApplication event loop, the closest headless analogue of how a
// ui_qml plugin actually runs. Same async chain as the headless e2e test, but
// now under the GUI/render event loop — the only context the fully-headless
// QJSEngine test could not replicate.
//
// Platform: defaults to "offscreen" so the nix check stays sandbox-friendly, but
// respects a pre-set QT_QPA_PLATFORM so the same binary can be run against a real
// display (QT_QPA_PLATFORM=cocoa) to exercise the *threaded* render loop — the
// one factor the offscreen basic render loop cannot reproduce.
//
// The sync-chain reproduction (logos.callModule spinning a nested waitForFinished
// loop, the deadlock-prone path) is gated behind LOGOS_GUI_SYNC_TEST=1 so it only
// runs in the explicit real-display experiment, never in the offscreen check.
#include "LogosQmlBridge.h"

#include "logos_api.h"
#include "token_manager.h"
#include "logos_provider_interface.h"
#include "module_proxy.h"
#include "remote_transport.h"
#include "logos_instance.h"
#include "logos_mode.h"

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QString>
#include <QTemporaryFile>
#include <QTest>
#include <QUrl>
#include <QVariant>
#include <QVariantList>

namespace {

class EchoProvider : public LogosProviderObject {
public:
    QVariant callMethod(const QString& method, const QVariantList& args) override {
        if (method == QLatin1String("echo") && !args.isEmpty())
            return args.first();
        return QVariant();
    }
    bool informModuleToken(const QString&, const QString&) override { return true; }
    QJsonArray getMethods() override { return QJsonArray{}; }
    void setEventListener(EventCallback) override {}
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("echo_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }
};

// Publish an echo provider + pre-seed the caller token so the bridge skips the
// capability_module handshake, then hand back a bridge wired to a QQuickView.
struct Fixture {
    RemoteTransportHost host;
    EchoProvider provider;
    ModuleProxy proxy;
    LogosAPI api;
    LogosQmlBridge bridge;

    Fixture()
        : host(LogosInstance::id("echo_module"))
        , proxy(&provider)
        , api(QStringLiteral("caller"))
        , bridge(&api)
    {
        proxy.saveToken(QStringLiteral("caller"), QStringLiteral("tok"));
        host.publishObject("echo_module", &proxy);
        LogosModeConfig::setMode(LogosMode::Remote);
        api.getTokenManager()->saveToken(QStringLiteral("echo_module"),
                                         QStringLiteral("tok"));
    }
};

} // namespace

class TestLogosQmlBridgeGui : public QObject {
    Q_OBJECT
private slots:
    // ASYNC chain: each reply callback issues the next call — the callModuleAsync
    // pattern of a QML-only plugin. Every call must complete.
    void chainedCallsInQmlScene_allComplete()
    {
        Fixture fx;

        QTemporaryFile qmlFile(QDir::tempPath() + QStringLiteral("/logos_gui_async_XXXXXX.qml"));
        QVERIFY(qmlFile.open());
        qmlFile.write(R"QML(
import QtQuick
Item {
    width: 320; height: 240
    property var results: []
    function fireNext(i) {
        if (i >= 12) return;
        logos.callModuleAsync("echo_module", "echo", [i], function (payload) {
            results.push(JSON.parse(payload));
            fireNext(i + 1);
        });
    }
    Component.onCompleted: {
        for (var e = 0; e < 14; ++e)
            logos.onModuleEvent("echo_module", "ev" + e);
        fireNext(0);
    }
}
)QML");
        qmlFile.flush();

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("logos"), &fx.bridge);
        view.setSource(QUrl::fromLocalFile(qmlFile.fileName()));
        QVERIFY2(view.status() == QQuickView::Ready,
                 qPrintable(QStringLiteral("QML load status=%1").arg(view.status())));
        view.show(); // starts the render/scene-graph loop (threaded on a real display)

        QObject* root = view.rootObject();
        QVERIFY(root);

        QElapsedTimer t; t.start();
        while (root->property("results").toList().size() < 12 && t.elapsed() < 25000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }

        const QVariantList results = root->property("results").toList();
        QCOMPARE(results.size(), 12);
        for (int i = 0; i < 12; ++i)
            QCOMPARE(results[i].toInt(), i);
    }

    // SYNC chain: logos.callModule spins a nested waitForFinished loop per call.
    // This is the deadlock-prone path under a threaded render loop, so it is gated
    // behind LOGOS_GUI_SYNC_TEST=1 and only exercised on a real display.
    void syncChainInQmlScene_allComplete()
    {
        if (qEnvironmentVariableIsEmpty("LOGOS_GUI_SYNC_TEST"))
            QSKIP("sync-chain reproduction only runs with LOGOS_GUI_SYNC_TEST=1 (real-display experiment)");

        Fixture fx;

        QTemporaryFile qmlFile(QDir::tempPath() + QStringLiteral("/logos_gui_sync_XXXXXX.qml"));
        QVERIFY(qmlFile.open());
        qmlFile.write(R"QML(
import QtQuick
Item {
    width: 320; height: 240
    property var results: []
    property bool done: false
    function runSync() {
        for (var i = 0; i < 12; ++i) {
            var r = logos.callModule("echo_module", "echo", [i]);
            results.push(JSON.parse(r));
        }
        done = true;
    }
    Component.onCompleted: {
        for (var e = 0; e < 14; ++e)
            logos.onModuleEvent("echo_module", "ev" + e);
        Qt.callLater(runSync); // defer out of onCompleted so the scene shows first
    }
}
)QML");
        qmlFile.flush();

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("logos"), &fx.bridge);
        view.setSource(QUrl::fromLocalFile(qmlFile.fileName()));
        QVERIFY2(view.status() == QQuickView::Ready,
                 qPrintable(QStringLiteral("QML load status=%1").arg(view.status())));
        view.show();

        QObject* root = view.rootObject();
        QVERIFY(root);

        QElapsedTimer t; t.start();
        while (!root->property("done").toBool() && t.elapsed() < 25000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }

        QVERIFY2(root->property("done").toBool(),
                 qPrintable(QStringLiteral("sync chain stalled: only %1/12 completed")
                                .arg(root->property("results").toList().size())));
        const QVariantList results = root->property("results").toList();
        QCOMPARE(results.size(), 12);
        for (int i = 0; i < 12; ++i)
            QCOMPARE(results[i].toInt(), i);
    }
};

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestLogosQmlBridgeGui tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_logos_qml_bridge_gui.moc"
