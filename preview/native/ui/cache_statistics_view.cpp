#include "cache_statistics_view.hpp"
#include "runtime_metrics_view.hpp"
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QShowEvent>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace biglybtc::ui {
namespace {
class Plot final:public QWidget {
public:
  explicit Plot(QWidget* parent):QWidget(parent){setObjectName(QStringLiteral("cache-rate-plot"));setMinimumHeight(180);setAccessibleName(tr("Observed disk read and write operations per second"));}
  void cacheFrame(const core::DiskActivityFrame& frame)noexcept{frame_=frame;}
protected:
  void paintEvent(QPaintEvent*)override{
    const auto frame=frame_;QPainter p(this);p.fillRect(rect(),palette().base());p.setRenderHint(QPainter::Antialiasing);
    const QRectF area=QRectF(rect()).adjusted(48,20,-18,-40);if(area.width()<=0||area.height()<=0){return;}
    p.setPen(palette().text().color());p.drawRect(area);p.drawText(QPointF(area.left(),14),tr("Operations/s — read (blue), write (orange)"));
    if(!frame.count){p.drawText(area,Qt::AlignCenter,tr("No observed disk operation intervals"));return;}
    std::int64_t maximum=1;for(std::size_t i=0;i<frame.count;++i){const auto& point=frame.points[i];
      if(point.readOperationsPerSecond){maximum=std::max(maximum,*point.readOperationsPerSecond);}if(point.writeOperationsPerSecond){maximum=std::max(maximum,*point.writeOperationsPerSecond);}}
    const auto first=frame.points[0].nanoseconds,last=frame.points[frame.count-1].nanoseconds;
    const long double span=static_cast<long double>(last)-static_cast<long double>(first);
    const auto position=[&](const core::DiskActivityPoint& point,std::int64_t rate){
      const auto x=span>0?(static_cast<long double>(point.nanoseconds)-static_cast<long double>(first))/span:0.5L;
      return QPointF(area.left()+static_cast<double>(x)*area.width(),area.bottom()-static_cast<double>(static_cast<long double>(rate)/maximum)*area.height());};
    for(int direction=0;direction<2;++direction){p.setPen(QPen(direction?QColor(210,100,20):QColor(30,100,210),2));p.setBrush(p.pen().color());
      for(std::size_t i=0;i<frame.count;++i){const auto& point=frame.points[i];const auto rate=direction?point.writeOperationsPerSecond:point.readOperationsPerSecond;if(!rate){continue;}
        const auto here=position(point,*rate);if(i&&point.connectedToPrevious){const auto& old=frame.points[i-1];const auto previous=direction?old.writeOperationsPerSecond:old.readOperationsPerSecond;
          if(previous){p.drawLine(position(old,*previous),here);}}
        p.drawEllipse(here,2,2);
      }
    }
    p.setPen(palette().text().color());p.drawText(QPointF(area.left(),area.bottom()+23),tr("Owner seconds: %1 to %2; actual observations only")
      .arg(static_cast<double>(first)/1e9,0,'f',2).arg(static_cast<double>(last)/1e9,0,'f',2));
  }
private:core::DiskActivityFrame frame_;
};
QString number(const std::optional<std::int64_t>& value){return value?QString::number(*value):QObject::tr("Unavailable");}
}
CacheStatisticsView::CacheStatisticsView(QWidget* parent):QWidget(parent){
  setObjectName(QStringLiteral("cache-statistics-view"));setAccessibleName(tr("Native disk and cache diagnostics"));auto* layout=new QVBoxLayout(this);
  tabs_=new QTabWidget(this);tabs_->setObjectName(QStringLiteral("cache-tabs"));summary_=new QWidget(tabs_);auto* summaryLayout=new QVBoxLayout(summary_);
  status_=new QLabel(tr("Disk observations unavailable"),summary_);status_->setObjectName(QStringLiteral("cache-status"));status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);summaryLayout->addWidget(status_);
  constexpr std::array<const char*,8> ids{"cache-jobs","cache-buffers-files","cache-operations","cache-blocks","cache-queued-write-bytes","cache-file-pool","cache-read-operations-rate","cache-write-operations-rate"};
  for(std::size_t i=0;i<facts_.size();++i){facts_[i]=new QLabel(tr("Unavailable"),summary_);facts_[i]->setObjectName(QString::fromLatin1(ids[i]));facts_[i]->setTextFormat(Qt::PlainText);facts_[i]->setWordWrap(true);summaryLayout->addWidget(facts_[i]);}
  plot_=new Plot(summary_);summaryLayout->addWidget(plot_,1);
  auto* note=new QLabel(tr("Disk activity for this session. Operation counts and queued write bytes use different units. This engine does not report cache capacity or cache-hit ratios."),summary_);
  note->setObjectName(QStringLiteral("cache-scope"));note->setTextFormat(Qt::PlainText);note->setWordWrap(true);summaryLayout->addWidget(note);
  tabs_->addTab(summary_,tr("Summary"));raw_=new RuntimeMetricsView(core::RuntimeMetricGroup::Disk,tabs_);raw_->setObjectName(QStringLiteral("statistics-cache-metrics"));tabs_->addTab(raw_,tr("Native counters"));layout->addWidget(tabs_);
  connect(tabs_,&QTabWidget::currentChanged,this,[this]{queueRefresh();});
}
bool CacheStatisticsView::beginSession(quint64 session){
  if(!session||session<=session_||revision_==std::numeric_limits<quint64>::max()){return false;}
  ++revision_;session_=session;packet_.reset();latestSnapshot_.reset();reducer_={};history_={};values_={};dirty_=true;
  if(plot_){static_cast<Plot*>(plot_.data())->cacheFrame(history_.snapshot());}
  QPointer<CacheStatisticsView> self(this);const auto revision=revision_;if(raw_){raw_->beginSession(session);}
  if(self&&self->revision_==revision){self->refresh();}return true;
}
bool CacheStatisticsView::setPresentation(RuntimeMetricsPresentation packet){
  if(!packet||!session_||packet->session!=session_||!packet->epoch||!packet->sequence||revision_==std::numeric_limits<quint64>::max()
      ||(packet_&&(packet->sequence<=packet_->sequence||packet->epoch<packet_->epoch))){return false;}
  const bool same=packet_&&packet_->epoch==packet->epoch;auto reducer=same?reducer_:core::DiskStatisticsReducer{};
  core::DiskStatistics values;auto history=same?history_:core::DiskActivityHistory{};
  if(packet->snapshot){const auto& source=*packet->snapshot;
    if(source.sequence>packet->sequence){return false;}
    if(same&&latestSnapshot_){if(source.sequence<latestSnapshot_->sequence||source.ownerElapsedNanoseconds<latestSnapshot_->ownerElapsedNanoseconds){return false;}
      if(source.sequence==latestSnapshot_->sequence&&(!packet_->snapshot||source.ownerElapsedNanoseconds!=latestSnapshot_->ownerElapsedNanoseconds||source.metrics!=latestSnapshot_->metrics)){return false;}}
    const auto projected=reducer.observe(source);if(!projected){return false;}
    try{for(const auto& metric:source.metrics){const auto text=QString::fromUtf8(metric.name.data(),static_cast<qsizetype>(metric.name.size()));
      const auto encoded=text.toUtf8();if(std::string_view(encoded.data(),static_cast<std::size_t>(encoded.size()))!=metric.name){return false;}}}catch(...){return false;}
    values=*projected;history.observe(values);
  }else{reducer.gap();history.gap();}
  ++revision_;const auto revision=revision_;packet_=std::move(packet);reducer_=reducer;values_=values;history_=history;dirty_=true;
  if(!same){latestSnapshot_.reset();}if(packet_->snapshot){latestSnapshot_=packet_->snapshot;}
  if(plot_){static_cast<Plot*>(plot_.data())->cacheFrame(history_.snapshot());}
  QPointer<CacheStatisticsView> self(this);const auto accepted=packet_;if(raw_){raw_->setPresentation(accepted);}
  if(self&&self->revision_==revision&&self->packet_==accepted){self->refresh();}return true;
}
void CacheStatisticsView::queueRefresh(){if(queued_){return;}queued_=true;QPointer<CacheStatisticsView> self(this);QMetaObject::invokeMethod(this,[self]{if(self){self->queued_=false;self->refresh();}},Qt::QueuedConnection);}
void CacheStatisticsView::refresh(){
  if(!dirty_||rendering_||!isVisible()||window()->isMinimized()||!summary_||!summary_->isVisible()||!status_||!plot_){return;}
  for(const auto& label:facts_){if(!label){return;}}
  QPointer<CacheStatisticsView> self(this);const auto revision=revision_;const auto packet=packet_;const auto status=status_;const auto facts=facts_;const auto plot=plot_;
  const auto current=[&]{return self&&self->revision_==revision&&self->packet_==packet&&self->isVisible()&&!self->window()->isMinimized()&&status&&plot&&self->summary_&&self->summary_->isVisible();};
  using F=core::DiskStatistic;const auto n=[&](F field){return number(values_.get(field));};
  const std::array<QString,8> text{{tr("Queued / running / fenced jobs: %1 / %2 / %3").arg(n(F::QueuedJobs)).arg(n(F::RunningJobs)).arg(n(F::FencedJobs)),
    tr("Buffers / pooled open files: %1 / %2").arg(n(F::Buffers)).arg(n(F::OpenFiles)),tr("Read / write operations: %1 / %2").arg(n(F::ReadOperations)).arg(n(F::WriteOperations)),
    tr("Read / write block counts: %1 / %2").arg(n(F::ReadBlocks)).arg(n(F::WriteBlocks)),tr("Queued write bytes: %1").arg(n(F::QueuedWriteBytes)),
    tr("File-handle pool hits / misses: %1 / %2").arg(n(F::FilePoolHits)).arg(n(F::FilePoolMisses)),
    tr("Read operations/s: %1").arg(number(values_.readOperationsPerSecond)),tr("Write operations/s: %1").arg(number(values_.writeOperationsPerSecond))}};
  const QString caption=!packet||!packet->snapshot?tr("Disk observations unavailable"):packet->current?tr("Observed native disk activity"):tr("Last observed native disk activity");
  rendering_=true;struct Finish{QPointer<CacheStatisticsView> self;quint64 revision;~Finish(){if(self){self->rendering_=false;if(self->dirty_&&self->revision_!=revision){self->queueRefresh();}}}}finish{self,revision};
  status->setText(caption);if(!current()){return;}
  for(std::size_t i=0;i<facts.size();++i){const auto label=facts[i];if(!current()||!label){return;}
    label->setText(text[i]);if(!current()||!label){return;}label->setToolTip(tr("Rates use the time between measurements. Missing or decreasing counters leave a gap in the graph."));
    if(!current()||!label){return;}label->setAccessibleDescription(text[i]);if(!current()||!label){return;}}
  if(current()){plot->update();dirty_=false;}
}
void CacheStatisticsView::showEvent(QShowEvent* event){QPointer<CacheStatisticsView> self(this);QWidget::showEvent(event);if(!self){return;}if(observedWindow_!=window()){if(observedWindow_){observedWindow_->removeEventFilter(this);}observedWindow_=window();observedWindow_->installEventFilter(this);}queueRefresh();}
bool CacheStatisticsView::eventFilter(QObject* object,QEvent* event){if(object==observedWindow_&&(event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange)){QPointer<CacheStatisticsView> self(this);QPointer<QObject> watched(object);setUpdatesEnabled(!window()->isMinimized());if(!self){return watched.isNull();}queueRefresh();}return QWidget::eventFilter(object,event);}
} // namespace biglybtc::ui
