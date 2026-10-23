#include "country_statistics_view.hpp"
#include <QAbstractTableModel>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <functional>
#include <limits>

namespace biglybtc::ui {
namespace {
constexpr int KeyRole = Qt::UserRole + 1;
QString countryName(const core::CountryRow& row) {
  switch (row.kind) {
    case core::CountryRowKind::Local: return QObject::tr("Local");
    case core::CountryRowKind::Unknown: return QObject::tr("Unknown country");
    case core::CountryRowKind::Unavailable: return QObject::tr("Country unavailable");
    case core::CountryRowKind::Country: {
      const auto end = std::find(row.name.begin(),row.name.end(),'\0');
      auto name = QString::fromUtf8(row.name.data(),static_cast<qsizetype>(end-row.name.begin()));
      name.replace(QLatin1Char('\n'),QChar(0x2424)).replace(QLatin1Char('\r'),QChar(0x240d)).replace(QLatin1Char('\t'),QChar(0x21e5));
      const auto code = QString::fromLatin1(row.code.data(),2);
      return name.isEmpty() ? code : code+QStringLiteral(" — ")+name;
    }
  }
  return {};
}
QString rowKey(const core::CountryRow& row) {
  return QString::number(static_cast<int>(row.kind))+QLatin1Char(':')+QString::fromLatin1(row.code.data(),2);
}
class CountryModel final : public QAbstractTableModel {
public:
  using QAbstractTableModel::QAbstractTableModel;
  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() || !packet_ || !packet_->snapshot ? 0 : static_cast<int>(packet_->snapshot->rowCount);
  }
  int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 7; }
  QVariant headerData(int section,Qt::Orientation orientation,int role) const override {
    if (orientation!=Qt::Horizontal || role!=Qt::DisplayRole) { return {}; }
    switch(section) {
      case 0:return tr("Country");case 1:return tr("Connections");
      case 2:return tr("Download speed (bytes/s)");case 3:return tr("Upload speed (bytes/s)");
      case 4:return tr("Coverage");case 5:return tr("Downloaded bytes");
      case 6:return tr("Uploaded bytes");default:return {};
    }
  }
  QVariant data(const QModelIndex& index,int role) const override {
    if (!index.isValid() || index.model()!=this || index.row()<0 || index.row()>=rowCount() || index.column()<0 || index.column()>=7) { return {}; }
    const auto& row=packet_->snapshot->rows[static_cast<std::size_t>(index.row())];
    const auto rate=index.column()==2?row.observedDownloadRate:index.column()==3?row.observedUploadRate:std::optional<std::int64_t>{};
    const auto prefix=index.column()==5?row.observedDownloadBytes:index.column()==6?row.observedUploadBytes:std::optional<std::int64_t>{};
    if (role==KeyRole) { return rowKey(row); }
    if (role==Qt::UserRole) {
      if (index.column()==1) { return QVariant::fromValue(static_cast<qulonglong>(row.rateObservedPeers)); }
      const auto number=index.column()>=5?prefix:rate;
      return number?QVariant::fromValue(static_cast<qlonglong>(*number)):QVariant{};
    }
    if (role==Qt::TextAlignmentRole && ((index.column()>=1 && index.column()<=3)||index.column()>=5)) { return static_cast<int>(Qt::AlignRight|Qt::AlignVCenter); }
    if (role==Qt::ToolTipRole) {
      return tr("Rates: %1 observed / %2 missing active; %3 retired. Byte prefixes: %4 download / %5 upload peer observations out of %6 retained records. First raw totals and lost intervals are excluded; tails are uncertified. No country lifetime/global total.")
          .arg(static_cast<qulonglong>(row.rateObservedPeers)).arg(static_cast<qulonglong>(row.rateMissingPeers))
          .arg(static_cast<qulonglong>(row.retiredPeers)).arg(static_cast<qulonglong>(row.downloadBytePeers))
          .arg(static_cast<qulonglong>(row.uploadBytePeers)).arg(static_cast<qulonglong>(row.recordedPeers))
          +(row.downloadBytesOverflow||row.uploadBytesOverflow?tr(" Overflowed byte sums are unavailable."):QString{});
    }
    if (role!=Qt::DisplayRole && role!=Qt::AccessibleTextRole) { return {}; }
    switch(index.column()) {
      case 0:return countryName(row);
      case 1:return QString::number(static_cast<qulonglong>(row.rateObservedPeers));
      case 2:case 3:return rate?QString::number(*rate):tr("Unavailable");
      case 5:case 6:return prefix?QString::number(*prefix):tr("Unavailable");
      case 4:return !packet_->current?tr("Last observed; partial"):(row.partial?tr("Partial observation"):tr("Observed rates; uncertain tail"));
    }
    return {};
  }
  bool replace(CountryStatisticsPresentation packet,const std::function<bool()>& current) {
    QPointer<CountryModel> guard(this);beginResetModel();if(!guard){return false;}
    const bool commit=current();if(!guard){return false;}if(commit){packet_=std::move(packet);}
    endResetModel();return commit&&guard;
  }
private:
  CountryStatisticsPresentation packet_;
};
class CountryProxy final : public QSortFilterProxyModel {
public:
  explicit CountryProxy(QObject* parent):QSortFilterProxyModel(parent) {setFilterKeyColumn(0);setFilterCaseSensitivity(Qt::CaseInsensitive);}
protected:
  bool lessThan(const QModelIndex& left,const QModelIndex& right) const override {
    if((left.column()>=1&&left.column()<=3)||left.column()>=5){
      const auto a=left.data(Qt::UserRole),b=right.data(Qt::UserRole);
      if(a.isValid()!=b.isValid()){return a.isValid();}
      if(a.isValid()&&a.toLongLong()!=b.toLongLong()){return a.toLongLong()<b.toLongLong();}
    }else{
      const auto order=QString::compare(left.data().toString(),right.data().toString(),Qt::CaseSensitive);if(order){return order<0;}
    }
    return left.data(KeyRole).toString()<right.data(KeyRole).toString();
  }
};
}
CountryStatisticsView::CountryStatisticsView(QWidget* parent):QWidget(parent) {
  setObjectName(QStringLiteral("country-statistics-view"));setAccessibleName(tr("Observed peer countries"));
  auto* layout=new QVBoxLayout(this);
  status_=new QLabel(tr("Country observations unavailable"),this);status_->setObjectName(QStringLiteral("country-status"));
  status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout->addWidget(status_);
  filter_=new QLineEdit(this);filter_->setObjectName(QStringLiteral("country-filter"));filter_->setMaxLength(256);
  filter_->setAccessibleName(tr("Search observed countries"));filter_->setPlaceholderText(tr("Search observed countries"));layout->addWidget(filter_);
  model_=new CountryModel(this);proxy_=new CountryProxy(this);proxy_->setSourceModel(model_);
  table_=new QTableView(this);table_->setObjectName(QStringLiteral("country-table"));table_->setAccessibleName(tr("Observed peer countries and payload rates"));
  table_->setModel(proxy_);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setWordWrap(false);table_->setSortingEnabled(true);table_->sortByColumn(0,Qt::AscendingOrder);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  constexpr std::array<int,7> widths{240,120,230,220,200,220,220};
  for(std::size_t i=0;i<widths.size();++i){table_->setColumnWidth(static_cast<int>(i),widths[i]);}layout->addWidget(table_,1);
  auto* scope=new QLabel(tr("Traffic measured during this session. Local network traffic stays separate. Completed connections keep their recorded bytes. Totals may be incomplete when measurements are missing."),this);
  scope->setTextFormat(Qt::PlainText);scope->setWordWrap(true);layout->addWidget(scope);
  connect(filter_,&QLineEdit::textChanged,this,[this](const QString& text){
    if(revision_==std::numeric_limits<quint64>::max()){return;}++revision_;search_=text;dirty_=true;queueRefresh();
  });
}
bool CountryStatisticsView::beginSession(quint64 session) {
  if(!session||session<=session_||revision_==std::numeric_limits<quint64>::max()){return false;}
  ++revision_;session_=session;packet_.reset();dirty_=true;refresh();return true;
}
bool CountryStatisticsView::setPresentation(CountryStatisticsPresentation packet) {
  if(revision_==std::numeric_limits<quint64>::max()||!acceptCountryStatistics(packet_,packet,session_)){return false;}
  ++revision_;packet_=std::move(packet);dirty_=true;refresh();return true;
}
void CountryStatisticsView::queueRefresh() {
  if(queued_){return;}queued_=true;QPointer<CountryStatisticsView> guard(this);
  QMetaObject::invokeMethod(this,[guard]{if(guard){guard->queued_=false;guard->refresh();}},Qt::QueuedConnection);
}
void CountryStatisticsView::refresh() {
  if(!dirty_||rendering_||!isVisible()||window()->isMinimized()||!status_||!filter_||!table_||!model_||!proxy_
      ||table_->model()!=proxy_||proxy_->sourceModel()!=model_){return;}
  const auto packet=packet_;const auto revision=revision_;QPointer<CountryStatisticsView> guard(this);
  const auto current=[&]{return guard&&guard->revision_==revision&&guard->packet_==packet&&guard->isVisible()&&!guard->window()->isMinimized();};
  const auto model=model_;const auto proxy=proxy_;const auto table=table_;const auto status=status_;
  const auto children=[&]{return current()&&model&&proxy&&table&&status&&guard->model_==model&&guard->proxy_==proxy
      &&guard->table_==table&&guard->status_==status&&table->model()==proxy&&proxy->sourceModel()==model;};
  rendering_=true;
  struct Finish {QPointer<CountryStatisticsView> view;quint64 revision;~Finish(){if(view){view->rendering_=false;if(view->dirty_&&view->revision_!=revision){view->queueRefresh();}}}} finish{guard,revision};
  const auto selected=packet&&renderedSession_==packet->session&&renderedEpoch_==packet->epoch?table->currentIndex().data(KeyRole):QVariant{};
  if(!static_cast<CountryModel*>(model.data())->replace(packet,children)||!children()){return;}
  proxy->setFilterFixedString(search_);if(!children()){return;}
  if(selected.isValid()&&!table->currentIndex().isValid()){
    for(int row=0;row<proxy->rowCount();++row){if(proxy->index(row,0).data(KeyRole)==selected){table->setCurrentIndex(proxy->index(row,0));if(!children()){return;}break;}}
  }
  renderedSession_=packet?packet->session:0;renderedEpoch_=packet?packet->epoch:0;
  QString text=tr("Country observations unavailable");
  if(packet&&packet->snapshot){
    const auto& value=*packet->snapshot;
    text=!packet->current?tr("Last observed peer countries; partial coverage")
        :!value.providerAvailable?tr("Country provider unavailable; local observations remain usable")
        :value.partial?tr("Observed peer countries; partial coverage"):tr("Observed peer countries; uncertain traffic tails");
    if(!value.rowCount){text+=tr("; no included peer observations");}
    if(value.ledgerIssues){text+=tr("; ledger diagnostics %1").arg(value.ledgerIssues);}
  }
  status->setText(text);if(!children()){return;}
  const auto tip=tr("Rates are last-observed payload low-pass samples, not exhaustive current country traffic. Missing classification and missing rate are independent.");
  status->setToolTip(tip);if(!children()){return;}status->setAccessibleDescription(text+QLatin1Char(' ')+tip);if(!children()){return;}
  dirty_=false;
}
void CountryStatisticsView::showEvent(QShowEvent* event) {
  QPointer<CountryStatisticsView> guard(this);QWidget::showEvent(event);if(!guard){return;}
  if(observedWindow_!=window()){if(observedWindow_){observedWindow_->removeEventFilter(this);}observedWindow_=window();observedWindow_->installEventFilter(this);}
  queueRefresh();
}
bool CountryStatisticsView::eventFilter(QObject* object,QEvent* event) {
  if(object==observedWindow_&&(event->type()==QEvent::WindowStateChange||event->type()==QEvent::Show)){
    QPointer<CountryStatisticsView> guard(this);QPointer<QObject> watched(object);setUpdatesEnabled(!window()->isMinimized());if(!guard){return watched.isNull();}queueRefresh();
  }
  return QWidget::eventFilter(object,event);
}
} // namespace biglybtc::ui
