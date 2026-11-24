#pragma once
class WarehouseClient;
class QWidget;
class QDialog;
// Caller owns the dialog; all asynchronous callbacks are guarded by its lifetime.
QDialog* createAdminDialog(WarehouseClient& client, QWidget* parent = nullptr);
void showAdminDialog(WarehouseClient& client, QWidget* parent);
