#include "temporal_recommendation_page.h"

#include <QSignalBlocker>

#include <QComboBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>

using namespace care_contract;
using recommendation_contract::PreferenceMode;
using recommendation_contract::PreferenceSelection;

namespace {

class TemporalPage final : public CarePage {
public:
    TemporalPage(CareService& service, std::function<BrowseScene()> scene)
        : CarePage(QStringLiteral("动态推荐")), service_(service), scene_(std::move(scene))
    {
        auto* options = new QHBoxLayout;
        addChoices(category_, QStringLiteral("偏好类别"),
            {QStringLiteral("音乐"), QStringLiteral("电影"), QStringLiteral("电视剧")});
        addChoices(type_, QStringLiteral("偏好格式"), {"MP3", "MP4", "WAV"});
        options->addWidget(new QLabel(QStringLiteral("类别")));
        options->addWidget(category_);
        options->addWidget(new QLabel(QStringLiteral("格式")));
        options->addWidget(type_);
        options->addStretch();
        options->addWidget(fetch_);
        options->addWidget(cancel_);
        layout->addLayout(options);
        list_->setAccessibleName(QStringLiteral("动态推荐结果"));
        list_->setObjectName(QStringLiteral("careTemporalList"));
        layout->addWidget(list_, 1);
        reason_->setTextFormat(Qt::PlainText);
        reason_->setWordWrap(true);
        layout->addWidget(reason_);
        auto* actions = new QHBoxLayout;
        actions->addWidget(feedback_);
        actions->addWidget(blocked_);
        actions->addStretch();
        layout->addLayout(actions);
        connect(fetch_, &QPushButton::clicked, this, [this] { request(); });
        connect(cancel_, &QPushButton::clicked, this, [this] {
            cancelRequest();
            status->setText(QStringLiteral("已停止等待推荐。"));
        });
        connect(list_, &QListWidget::currentRowChanged, this, [this] { updateSelection(); });
        connect(blocked_, &QPushButton::toggled, this, [this] { reloadView(); });
        connect(feedback_, &QPushButton::clicked, this, [this] { updateFeedback(); });
        blocked_->setCheckable(true);
        updateSelection();
        cancel_->setEnabled(false);
    }

    ~TemporalPage() override
    {
        // Cancellation may synchronously complete a callback while QPointer is
        // still valid during this destructor. Invalidate before calling out.
        CarePage::deactivate();
        auto handle = std::exchange(handle_, {});
        if (!handle.isEmpty())
            service_.cancelRecommendation(handle);
    }

    void activate() override { reloadView(); }

    void resetSession() override
    {
        deactivate();
        const QSignalBlocker blocker(blocked_);
        blocked_->setChecked(false);
        category_->setCurrentIndex(0);
        type_->setCurrentIndex(0);
    }

    void deactivate() override
    {
        CarePage::deactivate();
        writing_ = false;
        cancelRequest();
        clearRows();
        status->clear();
    }

private:
    void addChoices(QComboBox* box, const QString& name, const QStringList& values)
    {
        box->setAccessibleName(name);
        box->addItem(QStringLiteral("自动"));
        box->addItem(QStringLiteral("不限"));
        for (const auto& value : values)
            box->addItem(value);
    }

    PreferenceSelection preference(QComboBox* box) const
    {
        if (box->currentIndex() == 0)
            return {PreferenceMode::Auto, {}};
        if (box->currentIndex() == 1)
            return {PreferenceMode::Any, {}};
        return {PreferenceMode::Explicit, box->currentText()};
    }

    void reloadView()
    {
        cancelRequest();
        clearRows();
        const bool viewingBlocked = blocked_->isChecked();
        fetch_->setEnabled(!viewingBlocked);
        fetch_->setText(QStringLiteral("生成推荐"));
        if (!viewingBlocked) {
            request();
            return;
        }
        status->setText(QStringLiteral("正在读取已排除作品…"));
        loadBlocked(0);
    }

    void loadBlocked(qint64 offset)
    {
        service_.listWorkFeedback({offset, 50}, latest(2, [this](Result<Page<WorkFeedback>> result) {
            if (!result.status.ok() || !result.value) {
                reportError(result);
                return;
            }
            for (const auto& entry : result.value->items) {
                if (entry.feedback == WorkFeedbackKind::NotInterested)
                    entries_.append({entry.work, {}});
                else
                    continue;
                list_->addItem(entry.work.media.fileName);
            }
            status->setText(entries_.isEmpty() ? QStringLiteral("尚未排除作品。") : QStringLiteral("已排除作品"));
            nextBlocked_ = result.value->nextOffset;
            fetch_->setText(QStringLiteral("加载更多"));
            fetch_->setEnabled(nextBlocked_.has_value());
        }));
    }

    void request()
    {
        if (blocked_->isChecked()) {
            if (nextBlocked_)
                loadBlocked(*nextBlocked_);
            return;
        }
        if (pending_)
            return;
        clearRows();
        pending_ = true;
        cancel_->setEnabled(true);
        fetch_->setEnabled(false);
        status->setText(QStringLiteral("正在生成推荐…"));
        const auto handle = service_.recommendTemporal({preference(category_), preference(type_), scene_(), 10},
            latest(0, [this](Result<TemporalRecommendationResult> result) {
                pending_ = false;
                handle_.clear();
                cancel_->setEnabled(false);
                fetch_->setEnabled(true);
                if (!result.status.ok() || !result.value) {
                    reportError(result);
                    return;
                }
                const auto& data = *result.value;
                QStringList info{care_ui::timestamp(data.generatedAtUtc),
                    data.engine == RecommendationEngine::DeepSeek
                        ? QStringLiteral("DeepSeek") : QStringLiteral("时序推荐")};
                if (data.fallback)
                    info << QStringLiteral("备用结果");
                if (data.containsSyntheticData)
                    info << QStringLiteral("含模拟数据");
                info << (data.message.isEmpty() && data.items.isEmpty()
                            ? QStringLiteral("暂无匹配作品。") : data.message);
                status->setText(info.join(QStringLiteral(" · ")));
                entries_ = data.items;
                for (const auto& entry : entries_)
                    list_->addItem(entry.work.media.fileName);
            }));
        if (pending_)
            handle_ = handle;
    }

    void cancelRequest()
    {
        invalidate(0);
        invalidate(2);
        auto handle = std::exchange(handle_, {});
        pending_ = false;
        cancel_->setEnabled(false);
        fetch_->setEnabled(!blocked_->isChecked());
        if (!handle.isEmpty())
            service_.cancelRecommendation(handle);
    }

    void clearRows()
    {
        entries_.clear();
        nextBlocked_.reset();
        list_->clear();
        updateSelection();
    }

    void updateSelection()
    {
        const auto row = list_->currentRow();
        const bool selected = row >= 0 && row < entries_.size();
        feedback_->setText(blocked_->isChecked() ? QStringLiteral("撤销不感兴趣") : QStringLiteral("不感兴趣"));
        feedback_->setEnabled(selected && !writing_);
        reason_->setText(selected ? entries_.at(row).reason : QString());
    }

    void updateFeedback()
    {
        const auto row = list_->currentRow();
        if (row < 0 || row >= entries_.size() || writing_)
            return;
        const auto action = blocked_->isChecked() ? WorkFeedbackKind::Neutral : WorkFeedbackKind::NotInterested;
        writing_ = true;
        updateSelection();
        service_.setWorkFeedback({entries_.at(row).work.resource, action}, latest(1, [this](Status status) {
            writing_ = false;
            updateSelection();
            if (status.ok())
                reloadView();
            else
                reportError(status);
        }));
    }

    CareService& service_;
    std::function<BrowseScene()> scene_;
    QComboBox* category_ = new QComboBox;
    QComboBox* type_ = new QComboBox;
    QListWidget* list_ = new QListWidget;
    QLabel* reason_ = new QLabel;
    QPushButton* fetch_ = new QPushButton(QStringLiteral("生成推荐"));
    QPushButton* cancel_ = new QPushButton(QStringLiteral("取消"));
    QPushButton* feedback_ = new QPushButton;
    QPushButton* blocked_ = new QPushButton(QStringLiteral("查看已排除作品"));
    QList<RecommendedResource> entries_;
    std::optional<qint64> nextBlocked_;
    warehouse_contract::RecommendationHandle handle_;
    bool pending_ = false;
    bool writing_ = false;
};

} // namespace

CarePage* createTemporalPage(CareService& service, std::function<BrowseScene()> scene)
{
    return new TemporalPage(service, std::move(scene));
}
