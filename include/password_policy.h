#pragma once
#include <QString>

// Shared policy for new online passwords. Login keeps accepting existing hashes.
namespace online_password {
inline QString validationError(const QString& password)
{
    // Count Unicode code points: a Chinese character or supplementary emoji counts once.
    if (password.toUcs4().size() < 6)
        return QStringLiteral("密码长度至少为 6 个字符，中文、英文和数字均按字符计数。");
    if (password.toUtf8().size() > 1024)
        return QStringLiteral("密码过长，UTF-8 编码后不能超过 1024 字节。");
    return {};
}
}
