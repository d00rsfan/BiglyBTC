#include "saved_speed_manager_view.hpp"
#include "saved_speed_map_widget.hpp"
#include "live_speed_manager_view.hpp"
#include <QScrollArea>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <cmath>
#include <limits>
namespace biglybtc::ui {
namespace {
QString unavailable(){return QObject::tr("Unavailable");}
QString text(const std::optional<std::string>& raw){if(!raw){return unavailable();}auto value=QString::fromUtf8(raw->data(),static_cast<qsizetype>(raw->size()));const auto encoded=value.toUtf8();
 if(raw->find('\0')!=std::string::npos||std::string_view(encoded.data(),static_cast<std::size_t>(encoded.size()))!=*raw){return QObject::tr("Binary saved text");}return value;}
QString type(const std::optional<float>& value){if(!value||!std::isfinite(*value)){return unavailable();}if(*value==-0.1f){return QObject::tr("Unknown estimate");}if(*value==0){return QObject::tr("Estimated");}if(*value==0.5f){return QObject::tr("Choke-estimated");}if(*value==0.8f){return QObject::tr("Measured minimum");}if(*value==0.9f){return QObject::tr("Measured");}if(*value==1){return QObject::tr("Manual");}return QObject::tr("Other saved type %1").arg(*value,0,'g',9);}
QString estimate(const core::SavedSpeedEstimate* value){if(!value||!value->present||!value->bytesPerSecond||*value->bytesPerSecond<0){return unavailable();}return (*value->bytesPerSecond==0?QObject::tr("Unlimited (saved)"):QObject::tr("%1 bytes/s (saved)").arg(*value->bytesPerSecond))+QStringLiteral("; ")+type(value->type);}
QString details(const core::SavedSpeedEstimate* value){if(!value||!value->present){return unavailable();}const auto number=[](const auto& x){return x?QString::number(*x):unavailable();};
 return QObject::tr("Metric rating %1; hits %2; recorded at %3 epoch ms%4").arg(value->metric?QString::number(*value->metric,'g',9):unavailable(),number(value->hits),value->whenMillis&&*value->whenMillis>0?QString::number(*value->whenMillis):unavailable(),value->malformed?QObject::tr("; malformed saved fields"):QString{});}
}
SavedSpeedManagerView::SavedSpeedManagerView(QWidget* parent):QWidget(parent){setObjectName(QStringLiteral("saved-speed-manager-view"));
 auto* outer=new QVBoxLayout(this);auto* scroll=new QScrollArea(this);scroll->setObjectName(QStringLiteral("capacity-scroll"));scroll->setWidgetResizable(true);
 auto* content=new QWidget;content->setObjectName(QStringLiteral("capacity-content"));auto* layout=new QVBoxLayout(content);scroll->setWidget(content);outer->addWidget(scroll);
 live_=new LiveSpeedManagerView(content);layout->addWidget(live_);
 auto* savedHeading=new QLabel(tr("Saved mapper history and estimates"),content);savedHeading->setObjectName(QStringLiteral("saved-speed-heading"));savedHeading->setTextFormat(Qt::PlainText);layout->addWidget(savedHeading);
 const std::array<const char*,9> names{"saved-speed-status","saved-autospeed-policy","saved-speed-asn","saved-speed-algorithm","saved-speed-upload","saved-speed-upload-details","saved-speed-download","saved-speed-download-details","saved-speed-records"};
 for(std::size_t i=0;i<facts_.size();++i){facts_[i]=new QLabel(unavailable(),content);facts_[i]->setObjectName(QString::fromLatin1(names[i]));facts_[i]->setTextFormat(Qt::PlainText);facts_[i]->setWordWrap(true);facts_[i]->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);layout->addWidget(facts_[i]);}
 map_=new SavedSpeedMapWidget(content);layout->addWidget(map_,1);
 auto* scope=new QLabel(tr("Last-saved SpeedManager estimates and ASN data. They are not current path capacity, current ASN, live routes or active auto-speed measurements. Reading these files starts no ping, discovery or speed-adjustment service."),content);scope->setTextFormat(Qt::PlainText);scope->setWordWrap(true);layout->addWidget(scope);
 refresh_=new QPushButton(tr("Refresh saved estimates"),content);refresh_->setObjectName(QStringLiteral("saved-speed-refresh"));refresh_->setEnabled(false);layout->addWidget(refresh_);connect(refresh_,&QPushButton::clicked,this,[this]{queueRead(true);});}
bool SavedSpeedManagerView::beginSession(quint64 session){if(!session||session<=session_||revision_==std::numeric_limits<quint64>::max()){return false;}++revision_;session_=session;packet_.reset();livePacket_.reset();readSent_=false;explicitPending_=false;dirty_=true;
 QPointer<SavedSpeedManagerView> guard(this);const auto current=[&]{return guard&&guard->session_==session;};
 // Clear profile-owned child data even when the parent's rendering is deferred.
 const auto map=map_;if(map){map->setPresentation({},session);}if(!current()){return true;}
 const auto live=live_;if(live){live->beginSession(session);}if(!current()){return true;}
 if(live&&livePacket_){live->setPresentation(livePacket_);}if(!current()){return true;}
 if(refresh_){refresh_->setEnabled(true);}if(!current()){return true;}refresh();if(current()){guard->queueRead();}return true;}
void SavedSpeedManagerView::setLivePresentation(LiveSpeedManagerPresentation packet){
 if(!acceptLiveSpeedManager(livePacket_,packet,session_)){return;}livePacket_=std::move(packet);
 if(live_){live_->setPresentation(livePacket_);} // Final access: rendering callbacks may delete/rebind the parent.
}
bool SavedSpeedManagerView::setPresentation(SavedSpeedManagerPresentation packet){if(revision_==std::numeric_limits<quint64>::max()||!acceptSavedSpeedManager(packet_,packet,session_)){return false;}++revision_;packet_=std::move(packet);dirty_=true;refresh();return true;}
void SavedSpeedManagerView::queueRead(bool explicitRead){explicitPending_|=explicitRead;if(reading_){return;}reading_=true;QPointer<SavedSpeedManagerView> guard(this);QMetaObject::invokeMethod(this,[guard]{if(!guard){return;}guard->reading_=false;if(!guard->session_||!guard->isVisible()||guard->window()->isMinimized()||(guard->readSent_&&!guard->explicitPending_)){return;}guard->readSent_=true;guard->explicitPending_=false;const auto session=guard->session_;emit guard->readRequested(session);},Qt::QueuedConnection);}
void SavedSpeedManagerView::queueRefresh(){if(queued_){return;}queued_=true;QPointer<SavedSpeedManagerView> guard(this);QMetaObject::invokeMethod(this,[guard]{if(guard){guard->queued_=false;guard->refresh();}},Qt::QueuedConnection);}
void SavedSpeedManagerView::refresh(){if(!dirty_||!isVisible()||window()->isMinimized()){return;}QPointer<SavedSpeedManagerView> guard(this);const auto revision=revision_;const auto packet=packet_;const auto targets=facts_;
 const auto current=[&]{return guard&&guard->revision_==revision&&guard->packet_==packet&&guard->isVisible()&&!guard->window()->isMinimized();};
 std::array<QString,9> values;values.fill(unavailable());values[0]=tr("Saved estimates unavailable");values[1]=packet&&packet->livePolicy==AutoSpeedPolicy::Inactive?tr("Automatic speed adjustment inactive by native host policy"):tr("Live auto-speed status unavailable");
 if(packet){if(packet->state==SavedSpeedManagerState::Missing){values[0]=tr("No saved mapper history for the selected saved AS");}else if(packet->state==SavedSpeedManagerState::Failed){values[0]=packet->failure.isEmpty()?values[0]:tr("Saved estimate read failed: %1").arg(packet->failure);}else{values[0]=tr("Last-saved mapper estimates (%1)").arg(*packet->source==SavedSpeedManagerSource::Primary?tr("primary"):*packet->source==SavedSpeedManagerSource::Backup?tr("backup"):tr("saving recovery"));}
  if(packet->settings){const auto& s=*packet->settings;values[2]=tr("Saved AS %1; name %2; BGP prefix %3").arg(s.asNumber&&s.asNumber->empty()?tr("not recorded"):text(s.asNumber),text(s.asName),text(s.bgpPrefix));
   const auto boolean=[](const std::optional<bool>& value){return !value?unavailable():*value?QObject::tr("enabled"):QObject::tr("disabled");};values[3]=tr("Saved algorithm option %1; saved normal/seeding auto-upload: %2 / %3").arg(s.algorithm?QString::number(*s.algorithm):unavailable(),boolean(s.enabled),boolean(s.seedingEnabled));}
  const auto* h=packet->history.get();values[4]=tr("Upload capacity: %1").arg(estimate(h?&h->upload:nullptr));values[5]=details(h?&h->upload:nullptr);values[6]=tr("Download capacity: %1").arg(estimate(h?&h->download:nullptr));values[7]=details(h?&h->download:nullptr);values[8]=h&&h->savedPingRecords?tr("Saved ping-map records: %1 (not live pings or routes)").arg(*h->savedPingRecords):tr("Saved ping-map record count unavailable");}
 dirty_=false;
 for(std::size_t i=0;i<targets.size();++i){const auto target=targets[i];if(!current()||!target){if(guard){guard->dirty_=true;}return;}target->setText(values[i]);if(!current()||!target){if(guard){guard->dirty_=true;}return;}
  QString tip=tr("Last-read saved data only; not a current network measurement.");if(packet&&packet->settings&&i==3){tip+=tr(" Raw saved algorithm Long: %1.").arg(packet->settings->rawAlgorithm?QString::number(*packet->settings->rawAlgorithm):unavailable());}if(packet&&packet->history&&(i==4||i==6)){const auto& e=i==4?packet->history->upload:packet->history->download;tip+=tr(" Raw saved Long speed: %1; effective Java low32: %2.").arg(e.rawSpeed?QString::number(*e.rawSpeed):unavailable(),e.bytesPerSecond?QString::number(*e.bytesPerSecond):unavailable());}
  target->setToolTip(tip);if(!current()||!target){if(guard){guard->dirty_=true;}return;}target->setAccessibleDescription(values[i]);if(!current()){if(guard){guard->dirty_=true;}return;}}
 const auto map=map_;if(!current()||!map){if(guard){guard->dirty_=true;}return;}map->setPresentation(packet,session_);if(!current()){if(guard){guard->dirty_=true;}return;}
}
void SavedSpeedManagerView::showEvent(QShowEvent* event){QPointer<SavedSpeedManagerView> guard(this);QWidget::showEvent(event);if(!guard){return;}if(observedWindow_!=window()){if(observedWindow_){observedWindow_->removeEventFilter(this);}observedWindow_=window();observedWindow_->installEventFilter(this);}queueRefresh();queueRead();}
bool SavedSpeedManagerView::eventFilter(QObject* object,QEvent* event){if(object==observedWindow_&&(event->type()==QEvent::WindowStateChange||event->type()==QEvent::Show)){QPointer<SavedSpeedManagerView> guard(this);setUpdatesEnabled(!window()->isMinimized());if(!guard){return false;}queueRefresh();queueRead();}return QWidget::eventFilter(object,event);}
} // namespace biglybtc::ui
