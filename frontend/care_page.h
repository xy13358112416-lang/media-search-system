#pragma once

#include "care_contract.h"
#include <QDateTime>
#include <QHash>
#include <QLabel>
#include <QPointer>
#include <QTimeZone>
#include <QVBoxLayout>
#include <QWidget>
#include <utility>

class CarePage : public QWidget {
public:
    explicit CarePage(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent), layout(new QVBoxLayout(this)), status(new QLabel)
    {
        layout->setSpacing(10);
        auto* heading = new QLabel(title);
        heading->setObjectName(QStringLiteral("sectionTitle"));
        layout->addWidget(heading);
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setObjectName(QStringLiteral("careStatus"));
        layout->addWidget(status);
    }

    virtual void activate() {}
    virtual void deactivate() { ++epoch_; }

protected:
    template<class Callback>
    auto latest(int channel, Callback callback)
    {
        const auto serial = ++serials_[channel];
        return [self = QPointer<CarePage>(this), epoch = epoch_, channel, serial,
                callback = std::move(callback)](auto result) {
            if (self && self->epoch_ == epoch && self->serials_.value(channel) == serial)
                callback(std::move(result));
        };
    }

    void invalidate(int channel) { ++serials_[channel]; }

    template<class T>
    void reportError(const care_contract::Result<T>& result)
    {
        reportError(result.status);
    }

    void reportError(const care_contract::Status& result)
    {
        status->setText(result.message.isEmpty() ? QStringLiteral("请求失败，请重试。") : result.message);
    }

    QVBoxLayout* layout;
    QLabel* status;

private:
    quint64 epoch_ = 0;
    QHash<int, quint64> serials_;
};

namespace care_ui {
inline QTimeZone shanghai() { return QTimeZone(QByteArrayLiteral("Asia/Shanghai")); }

inline qint64 midnight(const QDate& date)
{
    return date.startOfDay(shanghai()).toSecsSinceEpoch();
}

inline QDateTime localTime(qint64 utc)
{
    return QDateTime::fromSecsSinceEpoch(utc, shanghai());
}

inline QString timestamp(qint64 utc)
{
    return localTime(utc).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

inline QString roleLabel(care_contract::PersonRole role)
{
    switch (role) {
    case care_contract::PersonRole::Actor: return QStringLiteral("演员");
    case care_contract::PersonRole::Director: return QStringLiteral("导演");
    case care_contract::PersonRole::Musician: return QStringLiteral("音乐人");
    }
    return {};
}
} // namespace care_ui
