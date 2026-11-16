#pragma once

#include "care_contract.h"

class QDialog;
class QWidget;

QDialog* createParticipationSettingsDialog(care_contract::CareService& service, QWidget* parent);
