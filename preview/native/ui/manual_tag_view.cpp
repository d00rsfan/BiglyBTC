#include "manual_tag_view.hpp"

#include <QAbstractTableModel>
#include <QByteArray>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <array>
#include <functional>
#include <limits>

namespace biglybtc::ui {
struct ManualTagTableData {
  std::shared_ptr<const core::ManualTagMembership> membership;
  std::vector<core::ManualTagRate> rates;
};
namespace {
constexpr int UidRole = Qt::UserRole + 1, RawNameRole = Qt::UserRole + 2,
    ResolvedRole = Qt::UserRole + 3, UnresolvedRole = Qt::UserRole + 4;
QString rateState(core::ManualTagRateState state) {
  using State = core::ManualTagRateState;
  switch (state) {
    case State::Unknown: return QObject::tr("Rate unavailable");
    case State::UnresolvedMembership: return QObject::tr("Unresolved membership");
    case State::AmbiguousIdentity: return QObject::tr("Ambiguous saved identity");
    case State::ObservedActive: return QObject::tr("Observed active target");
    case State::ObservedIdle: return QObject::tr("Observed idle");
    case State::ObservedOtherTagZero: return QObject::tr("Other tag; observed zero");
    case State::LastObserved: return QObject::tr("Last observed");
  }
  return QObject::tr("Rate unavailable");
}
QString tagName(const core::ManualTagRow& row) {
  if (!row.rawName || row.rawName->empty()) { return QObject::tr("Unnamed manual tag"); }
  const auto& raw = *row.rawName;
  auto text = QString::fromUtf8(raw.data(), static_cast<qsizetype>(raw.size()));
  const auto encoded = text.toUtf8();
  if (raw.find('\0') != std::string::npos
      || std::string_view(encoded.data(), static_cast<std::size_t>(encoded.size())) != raw) {
    return QObject::tr("Tag with binary saved name");
  }
  return text.replace(QLatin1Char('\n'), QChar(0x2424)).replace(QLatin1Char('\r'), QChar(0x240d))
      .replace(QLatin1Char('\t'), QChar(0x21e5));
}
class TagModel final : public QAbstractTableModel {
public:
  using QAbstractTableModel::QAbstractTableModel;
  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() || !data_ || !data_->membership ? 0 : static_cast<int>(data_->rates.size());
  }
  int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 6; }
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) { return {}; }
    switch (section) {
      case 0: return tr("Manual tag"); case 1: return tr("UID"); case 2: return tr("Membership");
      case 3: return tr("Download bytes/s"); case 4: return tr("Upload bytes/s"); case 5: return tr("Observation");
      default: return {};
    }
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount()
        || index.column() < 0 || index.column() >= 6) { return {}; }
    const auto i = static_cast<std::size_t>(index.row());
    const auto& row = data_->membership->rows()[i]; const auto& rate = data_->rates[i];
    if (role == UidRole) { return row.uid && !row.ambiguousIdentity ? QVariant::fromValue(static_cast<qlonglong>(*row.uid)) : QVariant{}; }
    if (role == RawNameRole) { return row.rawName ? QVariant(QByteArray(row.rawName->data(),static_cast<qsizetype>(row.rawName->size()))) : QVariant{}; }
    if (role == ResolvedRole || role == UnresolvedRole) {
      return row.membershipKnown ? QVariant::fromValue(static_cast<qulonglong>(role == ResolvedRole ? row.resolvedCount : row.unresolvedCount)) : QVariant{};
    }
    const auto numeric = index.column() == 1 ? row.uid : index.column() == 3 ? rate.downloadRate
        : index.column() == 4 ? rate.uploadRate : std::optional<std::int64_t>{};
    if (role == Qt::UserRole) { return numeric ? QVariant::fromValue(static_cast<qlonglong>(*numeric)) : QVariant{}; }
    if (role == Qt::TextAlignmentRole && (index.column() == 1 || index.column() == 3 || index.column() == 4)) {
      return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role == Qt::ToolTipRole) {
      if (index.column() == 0 && row.rawName) {
        // Hex is safe even when stored bytes resemble rich text or contain NUL.
        return tr("Stored name bytes (hex): %1%2").arg(QString::fromLatin1(QByteArray(row.rawName->data(),
            static_cast<qsizetype>(row.rawName->size())).toHex(' ')))
            .arg(row.nameMayRequireLocalization ? tr("; saved name key") : QString{});
      }
      return tr("Native-library membership and target peer payload + protocol rates only. Overlapping tags are independent; do not sum these rows as global traffic.");
    }
    if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) { return {}; }
    switch (index.column()) {
      case 0: return tagName(row);
      case 1: return row.uid ? QString::number(*row.uid) : tr("Unknown");
      case 2: return row.membershipKnown ? tr("%1 resolved / %2 unresolved").arg(row.resolvedCount).arg(row.unresolvedCount) : tr("Unknown membership");
      case 3: case 4: return numeric ? QString::number(*numeric) : tr("Unavailable");
      case 5: return rateState(rate.state);
    }
    return {};
  }
  bool replace(std::shared_ptr<const ManualTagTableData> data, const std::function<bool()>& current) {
    QPointer<TagModel> guard(this); beginResetModel(); if (!guard) { return false; }
    const bool commit = current(); if (!guard) { return false; }
    if (commit) { data_ = std::move(data); }
    endResetModel(); return commit && guard;
  }
private:
  std::shared_ptr<const ManualTagTableData> data_;
};
class TagProxy final : public QSortFilterProxyModel {
public:
  explicit TagProxy(QObject* parent) : QSortFilterProxyModel(parent) {
    setFilterKeyColumn(0); setFilterCaseSensitivity(Qt::CaseInsensitive);
  }
protected:
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
    if (left.column() == 1 || left.column() == 3 || left.column() == 4) {
      const auto a = left.data(Qt::UserRole), b = right.data(Qt::UserRole);
      if (a.isValid() != b.isValid()) { return a.isValid(); }
      if (a.isValid() && a.toLongLong() != b.toLongLong()) { return a.toLongLong() < b.toLongLong(); }
    } else {
      const auto order = QString::compare(left.data().toString(),right.data().toString(),Qt::CaseSensitive);
      if (order) { return order < 0; }
    }
    return left.data(UidRole).toLongLong() < right.data(UidRole).toLongLong();
  }
};
}
ManualTagStatisticsView::ManualTagStatisticsView(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("manual-tag-statistics-view")); setAccessibleName(tr("Manual tag transfer rates"));
  auto* layout = new QVBoxLayout(this);
  status_ = new QLabel(tr("Manual tag data unavailable"),this);status_->setObjectName(QStringLiteral("manual-tag-status"));
  status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout->addWidget(status_);
  filter_ = new QLineEdit(this);filter_->setObjectName(QStringLiteral("manual-tag-filter"));filter_->setMaxLength(256);
  filter_->setAccessibleName(tr("Search manual tag names"));filter_->setPlaceholderText(tr("Search manual tag names"));layout->addWidget(filter_);
  model_ = new TagModel(this);proxy_ = new TagProxy(this);proxy_->setSourceModel(model_);
  table_ = new QTableView(this);table_->setObjectName(QStringLiteral("manual-tag-table"));table_->setAccessibleName(tr("Manual tag membership and observed native target rates"));
  table_->setModel(proxy_);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setWordWrap(false);table_->setSortingEnabled(true);table_->sortByColumn(0,Qt::AscendingOrder);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  const std::array<int,6> widths{230,150,210,130,130,180};
  for (std::size_t i = 0; i < widths.size(); ++i) { table_->setColumnWidth(static_cast<int>(i),widths[i]); }
  layout->addWidget(table_,1);
  auto* scope = new QLabel(tr("Saved manual tags, resolved against this native library. Rates describe observed target peer payload + protocol. Unresolved members stay unavailable. Other-tag zero requires complete owner coverage. Automatic tags and rule-generated membership are not included."),this);
  scope->setTextFormat(Qt::PlainText);scope->setWordWrap(true);layout->addWidget(scope);
  refresh_ = new QPushButton(tr("Refresh tags"),this);refresh_->setObjectName(QStringLiteral("manual-tag-refresh"));refresh_->setEnabled(false);layout->addWidget(refresh_);
  connect(refresh_,&QPushButton::clicked,this,[this]{queueRead(true);});
  connect(filter_,&QLineEdit::textChanged,this,[this](const QString& text){
    if (revision_ == std::numeric_limits<quint64>::max()) { return; }
    ++revision_;search_=text;dirty_=true;queueRefresh();
  });
}
bool ManualTagStatisticsView::beginSession(quint64 session) {
  if (!session || session <= session_ || revision_ == std::numeric_limits<quint64>::max()) { return false; }
  ++revision_;session_=session;packet_.reset();data_.reset();readSent_=false;explicitReadPending_=false;dirty_=true;
  QPointer<ManualTagStatisticsView> guard(this);if (refresh_) { refresh_->setEnabled(true); }
  if (!guard) { return true; } refresh(); if (guard) { guard->queueRead(); } return true;
}
bool ManualTagStatisticsView::setPresentation(ManualTagStatisticsPresentation packet) {
  if (revision_ == std::numeric_limits<quint64>::max()
      || !acceptManualTagStatistics(packet_,packet,session_)) { return false; }
  try {
    auto data = std::make_shared<ManualTagTableData>();
    switch (packet->readState) {
      case ManualTagReadState::Ready: {
        const auto& member = packet->membership;
        auto rates = core::projectManualTagRates(*member,packet->traffic);if (!rates) { return false; }
        data->membership=member;data->rates=std::move(*rates);break;
      }
      case ManualTagReadState::Missing: case ManualTagReadState::Failed:
        if (packet->membership || packet->traffic.state != core::ManualTagTrafficState::Unavailable
            || (packet->readState == ManualTagReadState::Missing && !packet->failure.isEmpty())) { return false; } break;
      default: return false;
    }
    ++revision_;packet_=std::move(packet);data_=std::move(data);dirty_=true;refresh();return true;
  } catch (...) { return false; }
}
void ManualTagStatisticsView::queueRead(bool explicitRead) {
  explicitReadPending_ |= explicitRead;
  if (readQueued_) { return; }readQueued_=true;QPointer<ManualTagStatisticsView> guard(this);
  QMetaObject::invokeMethod(this,[guard]{
    if (!guard) { return; }guard->readQueued_=false;
    if (!guard->session_ || !guard->isVisible() || guard->window()->isMinimized()
        || (guard->readSent_ && !guard->explicitReadPending_)) { return; }
    guard->readSent_=true;guard->explicitReadPending_=false;
    const auto session=guard->session_;emit guard->readRequested(session);
  },Qt::QueuedConnection);
}
void ManualTagStatisticsView::queueRefresh() {
  if (queued_) { return; }queued_=true;QPointer<ManualTagStatisticsView> guard(this);
  QMetaObject::invokeMethod(this,[guard]{if (guard) { guard->queued_=false;guard->refresh(); }},Qt::QueuedConnection);
}
void ManualTagStatisticsView::refresh() {
  if (!dirty_ || rendering_ || !isVisible() || window()->isMinimized() || !status_ || !filter_ || !table_ || !model_ || !proxy_
      || table_->model() != proxy_ || proxy_->sourceModel() != model_) { return; }
  const auto packet=packet_;const auto data=data_;const auto revision=revision_;QPointer<ManualTagStatisticsView> guard(this);
  const auto current=[&]{return guard && guard->revision_==revision && guard->packet_==packet && guard->data_==data
      && guard->isVisible() && !guard->window()->isMinimized();};
  const auto model=model_;const auto proxy=proxy_;const auto table=table_;const auto status=status_;
  const auto children=[&]{return current() && model && proxy && table && status && guard->model_==model
      && guard->proxy_==proxy && guard->table_==table && guard->status_==status && table->model()==proxy && proxy->sourceModel()==model;};
  rendering_=true;
  struct Finish {QPointer<ManualTagStatisticsView> view;quint64 revision;~Finish(){if(view){view->rendering_=false;if(view->dirty_&&view->revision_!=revision){view->queueRefresh();}}}} finish{guard,revision};
  const auto selected=packet && renderedSession_==packet->session && renderedEpoch_==packet->epoch ? table->currentIndex().data(UidRole) : QVariant{};
  if (!static_cast<TagModel*>(model.data())->replace(data,children) || !children()) { return; }
  proxy->setFilterFixedString(search_);if (!children()) { return; }
  if (selected.isValid() && !table->currentIndex().isValid()) {
    for (int row=0;row<proxy->rowCount();++row) {if(proxy->index(row,0).data(UidRole)==selected){table->setCurrentIndex(proxy->index(row,0));if(!children()){return;}break;}}
  }
  renderedSession_=packet?packet->session:0;renderedEpoch_=packet?packet->epoch:0;
  QString text=tr("Manual tag data unavailable");
  if (packet) {
    if (packet->readState==ManualTagReadState::Missing) {text=tr("No saved manual tag configuration");}
    else if (packet->readState==ManualTagReadState::Failed) {text=packet->failure.isEmpty()?tr("Manual tag data unavailable"):tr("Manual tag read failed: %1").arg(packet->failure);}
    else if (!packet->membership->manualTypePresent()) {text=tr("Saved document has no manual-download tags section");}
    else if (packet->membership->rows().empty()) {text=tr("No visible saved manual-download tags");}
    else if (packet->traffic.state==core::ManualTagTrafficState::Unavailable) {text=tr("Manual tags loaded; target rates unavailable");}
    else if (!packet->traffic.current) {text=tr("Last observed manual-tag rates");}
    else {text=tr("Observed native target rates; unknown memberships remain unavailable");}
  }
  status->setText(text);if (!children()) { return; }
  const auto tip=packet?tr("Rates from this native transfer engine. Other-tag zero is shown only when all owned active targets are covered.")
      :tr("No observation available; a missing observation is not zero");
  status->setToolTip(tip);if (!children()) { return; }status->setAccessibleDescription(text+QLatin1Char(' ')+tip);if(!children()){return;}
  dirty_=false;
}
void ManualTagStatisticsView::showEvent(QShowEvent* event) {
  QPointer<ManualTagStatisticsView> guard(this);QWidget::showEvent(event);if(!guard){return;}
  if (observedWindow_!=window()) {if(observedWindow_){observedWindow_->removeEventFilter(this);}observedWindow_=window();observedWindow_->installEventFilter(this);}
  queueRefresh();queueRead();
}
bool ManualTagStatisticsView::eventFilter(QObject* object,QEvent* event) {
  if (object==observedWindow_ && (event->type()==QEvent::WindowStateChange || event->type()==QEvent::Show)) {
    QPointer<ManualTagStatisticsView> guard(this);setUpdatesEnabled(!window()->isMinimized());if(!guard){return false;}
    queueRefresh();queueRead();
  }
  return QWidget::eventFilter(object,event);
}
} // namespace biglybtc::ui
