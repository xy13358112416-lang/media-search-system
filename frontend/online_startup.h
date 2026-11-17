#pragma once

#include "warehouse_contract.h"

class QWidget;

namespace online_ui {

struct ConnectionSettings {
    QString host = QStringLiteral("192.168.43.8");
    quint16 port = 8765;
};

// Defaults, then [server] in media-client.ini, then the last successful endpoint.
// An explicit configPath replaces the application-directory INI path.
ConnectionSettings loadConnectionSettings(const QString& configPath = {});

// Keeps service connected on success and disconnects it when sign-in is cancelled.
bool signIn(warehouse_contract::WarehouseService& service,
            warehouse_contract::UserInfo& user, QWidget* parent = nullptr,
            const QString& configPath = {});

} // namespace online_ui
