#pragma once

#include "care_contract.h"
#include <QDialog>

class CareDashboard final : public QDialog {
public:
    CareDashboard(care_contract::CareService& service, std::function<care_contract::BrowseScene()> scene,
                  bool demonstration, QWidget* parent = nullptr);
};

void showCareDemonstration(QWidget* parent, care_contract::BrowseScene scene);
