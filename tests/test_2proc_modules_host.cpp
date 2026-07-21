// Process A of the 2-process reproduction: publishes echo_module +
// capability_module over QtRO local sockets (LogosInstance::id-derived, shared
// via the inherited LOGOS_INSTANCE_ID env), then runs its own event loop —
// exactly like real module subprocesses. Prints "READY" once both are published.
#include "test_2proc_common.h"

#include "remote_transport.h"
#include "logos_instance.h"
#include "logos_mode.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace twoproc;

    LogosModeConfig::setMode(LogosMode::Remote);

    EchoProvider echo;
    echo.fireEvents = !qEnvironmentVariableIsEmpty("LOGOS_FIRE_EVENTS");
    ModuleProxy echoProxy(&echo);
    RemoteTransportHost echoHost(LogosInstance::id("echo_module"));
    if (!echoHost.publishObject("echo_module", &echoProxy)) {
        QTextStream(stderr) << "FAILED to publish echo_module\n";
        return 1;
    }

    CapabilityProvider cap;
    cap.echoProxy = &echoProxy;
    cap.rotate = !qEnvironmentVariableIsEmpty("LOGOS_ROTATE_TOKENS");
    ModuleProxy capProxy(&cap);
    capProxy.saveToken(QStringLiteral("caller"), QStringLiteral("cap-token"));
    RemoteTransportHost capHost(LogosInstance::id("capability_module"));
    if (!capHost.publishObject("capability_module", &capProxy)) {
        QTextStream(stderr) << "FAILED to publish capability_module\n";
        return 1;
    }

    QTextStream out(stdout);
    out << "READY " << LogosInstance::id("echo_module") << "\n";
    out.flush();

    return app.exec();
}
