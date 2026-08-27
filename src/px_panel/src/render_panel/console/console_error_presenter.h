#pragma once

#include <QString>
#include <string>

#include "px_console_client/console_errors.h"

namespace px {

    enum class ConsoleErrorOperation {
        kConnectRemote,
        kSignIn,
        kSignOut,
        kRegister,
        kUpdateAccount,
        kLoadResources,
        kStartApplication,
        kStopApplication,
        kFileTransfer,
        kUpdateDevice,
        kCheckConsole,
    };

    QString MakeConsoleEndpoint(const std::string& host, int port);

    QString MakeConsoleErrorMessage(ConsoleErrorOperation operation,
                                    px_console::ConsoleApiError error,
                                    const std::string& server_message,
                                    const QString& endpoint = {});

}
