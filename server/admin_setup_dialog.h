#pragma once
#include <QDialog>
#include <memory>
class WarehouseServer;
class QLineEdit;
class QLabel;
class QPushButton;

// Server-local setup tool. It never opens a network listener or calls a remote API.
class AdminSetupDialog final : public QDialog {
public:
    explicit AdminSetupDialog(const QString& databasePath, QWidget* parent = nullptr);
    ~AdminSetupDialog() override;
private:
    void loadDatabase();
    void submit();
    void lockForm(bool locked);
    std::unique_ptr<WarehouseServer> server_;
    QLineEdit *database_, *username_, *password_, *confirmation_;
    QLabel* status_;
    QPushButton* create_;
};
