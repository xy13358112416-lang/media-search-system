#pragma once

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>

namespace account_ui {

inline QString avatarPath(const QString& databasePath, qint64 userId)
{
    return QDir(databasePath + QStringLiteral(".avatars"))
        .filePath(QString::number(userId) + QStringLiteral(".png"));
}

inline bool saveAvatar(const QString& source, const QString& destination, QString& error)
{
    constexpr qint64 maxBytes = 20 * 1024 * 1024;
    const QFileInfo file(source);
    QImageReader reader(source);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!file.isFile() || file.size() > maxBytes || !size.isValid() ||
        size.width() > 8192 || size.height() > 8192) {
        error = QStringLiteral("请选择一张可读取的图片，大小不超过 20 MB，宽高均不超过 8192 像素。");
        return false;
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        error = QStringLiteral("无法读取这张图片，请换一张。");
        return false;
    }
    const int side = qMin(image.width(), image.height());
    const QImage thumbnail = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)
        .scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        error = QStringLiteral("无法创建头像目录。");
        return false;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || !thumbnail.save(&output, "PNG") || !output.commit()) {
        error = QStringLiteral("无法保存头像。");
        return false;
    }
    return true;
}

inline QIcon avatarIcon(const QString& path, const QString& username)
{
    QPixmap pixmap(96, 96);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath circle;
    circle.addEllipse(pixmap.rect());
    painter.setClipPath(circle);
    const QImage image(path);
    if (image.isNull()) {
        painter.fillRect(pixmap.rect(), QColor(QStringLiteral("#dceaff")));
        QFont font = painter.font();
        font.setPixelSize(40);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(QStringLiteral("#173253")));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, username.left(1).toUpper());
    } else {
        painter.drawImage(pixmap.rect(), image);
    }
    painter.end();
    return QIcon(pixmap);
}

inline QPushButton* createAvatarButton(const QString& databasePath, qint64 userId, const QString& username)
{
    const QString path = avatarPath(databasePath, userId);
    auto* button = new QPushButton;
    button->setObjectName(QStringLiteral("avatarButton"));
    button->setAccessibleName(QStringLiteral("更换头像"));
    button->setToolTip(QStringLiteral("更换头像"));
    button->setFixedSize(44, 44);
    button->setIconSize(QSize(40, 40));
    button->setIcon(avatarIcon(path, username));
    QObject::connect(button, &QPushButton::clicked, button, [button, path, username] {
        const QString source = QFileDialog::getOpenFileName(button, QStringLiteral("选择头像图片"), {},
            QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
        if (source.isEmpty())
            return;
        QString error;
        if (!saveAvatar(source, path, error)) {
            QMessageBox::warning(button, QStringLiteral("头像保存失败"), error);
            return;
        }
        button->setIcon(avatarIcon(path, username));
    });
    return button;
}

} // namespace account_ui
