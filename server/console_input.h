#pragma once
#include <QTextStream>
#include <QString>
#include <optional>

namespace console_input {
bool isTerminal();
// Secret terminal input disables echo and restores its mode before returning.
// Redirected input remains supported for existing deployment scripts.
std::optional<QString> readLine(QTextStream& input, QTextStream& output,
                                const QString& prompt, bool secret, QString& error);
}
