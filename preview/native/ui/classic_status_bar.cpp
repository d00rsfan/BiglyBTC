// SPDX-License-Identifier: GPL-2.0-or-later
#include "classic_status_bar.hpp"
#include "native_activity_rates.hpp"
#include <QCoreApplication>
#include <QLabel>
#include <QToolButton>
#include <QResizeEvent>
#include <QFontMetrics>
#include <QStyle>
#include <QStringList>
#include <QLocale>
#include <limits>
namespace biglybtc::ui {
namespace {
QString tip(QString text){return QStringLiteral("<qt>")+text.toHtmlEscaped().replace('\n',QStringLiteral("<br>"))+QStringLiteral("</qt>");}
}
QToolButton* ClassicStatusBar::field(const char* id,const QString& text,const QString& section){
 auto* button=new QToolButton(this);button->setObjectName(QString::fromLatin1(id));button->setAutoRaise(true);button->setText(text);button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);addPermanentWidget(button);
 connect(button,&QToolButton::clicked,this,[this,section]{emit statisticsRequested(section);});return button;
}
ClassicStatusBar::ClassicStatusBar(QWidget* parent):QStatusBar(parent),version_(new QLabel(this)){
 setObjectName("classic-status-bar");setSizeGripEnabled(true);version_->setObjectName("classic-status-version");version_->setTextFormat(Qt::PlainText);addWidget(version_);
 mainline_=field("status-mainline","mDHT: —","statistics-all-metrics");ratio_=field("status-share-ratio","Ratio: —","statistics-transfers");network_=field("status-network","IP: —","statistics-transfers");nat_=field("status-nat","NAT: —","statistics-all-metrics");ips_=field("status-ip-filter","IPs: —","statistics-all-metrics");down_=field("status-download","↓ —","statistics-transfers");up_=field("status-upload","↑ —","statistics-transfers");
 down_->setMinimumWidth(100);up_->setMinimumWidth(100);network_->setMaximumWidth(170);render();
}
void ClassicStatusBar::beginSession(quint64 session){session_=session;native_.reset();runtime_.reset();ratioKnown_=false;render();}
void ClassicStatusBar::setSettings(ClassicStatusSettings settings,TransferDisplaySettings units){settings_=settings;units_=units;render();}
void ClassicStatusBar::setSessionSnapshot(NativeSessionPresentation packet){if(!acceptNativeSession(native_,packet,session_)){return;}native_=std::move(packet);render();}
void ClassicStatusBar::setRuntimeSnapshot(RuntimeMetricsPresentation packet){if(!packet||packet->session!=session_||!packet->epoch||!packet->sequence||(runtime_&&(packet->epoch<runtime_->epoch||(packet->epoch==runtime_->epoch&&packet->sequence<=runtime_->sequence)))){return;}runtime_=std::move(packet);render();}
void ClassicStatusBar::setOverall(qint64 downloaded,qint64 uploaded,bool available){downloaded_=downloaded;uploaded_=uploaded;ratioKnown_=available&&downloaded>=0&&uploaded>=0;render();}
void ClassicStatusBar::setOffline(bool value){offline_=value;render();}
std::optional<qint64> ClassicStatusBar::metric(std::string_view name)const{
 if(offline_||!runtime_||!runtime_->current||!runtime_->snapshot){return {};}
 for(const auto& value:runtime_->snapshot->metrics){if(value.name==name&&value.value>=0){return value.value;}}return {};
}
void ClassicStatusBar::visibility(){ratio_->setVisible(settings_.shareRatio&&width()>=850);network_->setVisible(settings_.networkInfo&&width()>=1050);nat_->setVisible(settings_.nat&&width()>=850);ips_->setVisible(settings_.ipFilter);}
void ClassicStatusBar::resizeEvent(QResizeEvent* event){QStatusBar::resizeEvent(event);visibility();}
void ClassicStatusBar::render(){
 auto version=QStringLiteral("BiglyBTC");if(!QCoreApplication::applicationVersion().isEmpty()){version+=' '+QCoreApplication::applicationVersion();}version_->setText(version);version_->setToolTip(tip(version+tr("\nQt %1\n%2").arg(QString::fromLatin1(qVersion()),offline_?tr("Offline preview"):tr("Current native session"))));
 for(bool sending:{false,true}){
  auto* button=sending?up_:down_;const auto arrow=sending?QStringLiteral("↑ "):QStringLiteral("↓ ");QString value=QStringLiteral("—"),details;
  const bool live=!offline_&&native_&&(native_->state==NativeSessionState::Idle||native_->state==NativeSessionState::Active||native_->state==NativeSessionState::Retiring);
  if(live){const auto rates=nativeActivityRates(native_->sample?native_->sample->rates:std::nullopt,native_->sourceRates,native_->sourceRates?NativeActivityRateMode::Source10s:NativeActivityRateMode::Interval,sending);
   if(rates[0]&&rates[1]&&rates[2]){
    value=transferBytes(settings_.dataOnly?*rates[1]:settings_.separateProtocol?*rates[1]:*rates[0],units_,true);
    if(settings_.separateProtocol&&!settings_.dataOnly){value+=" ("+transferBytes(*rates[2],units_,true)+')';}
    details=tr("Payload: %1\nProtocol: %2").arg(transferBytes(*rates[1],units_,true),transferBytes(*rates[2],units_,true));
   }
   const auto limit=sending?native_->rateLimits.upload:native_->rateLimits.download;
   if(limit.state==core::RateLimitState::Limited){value='['+transferBytes(limit.bytesPerSecond,units_,true)+"] "+value;details+=tr("\nEffective limit: %1").arg(transferBytes(limit.bytesPerSecond,units_,true));}
   else{details+=limit.state==core::RateLimitState::Unlimited?tr("\nEffective limit: unlimited"):tr("\nEffective limit unavailable");}
  }
  button->setText(arrow+value);button->setToolTip(tip((sending?tr("Upload"):tr("Download"))+(details.isEmpty()?tr("\nWaiting for a current measurement"):QStringLiteral("\n")+details)+tr("\nClick to open transfer statistics")));
 }
 QString mainline=tr("mDHT: —"),mainlineTip=tr("Mainline DHT status is not available yet");
 if(!offline_&&runtime_&&runtime_->current&&runtime_->mainlineObserved){
  switch(runtime_->mainlineState){case 0:mainline=tr("mDHT: disabled");break;case 1:mainline=tr("mDHT: active");break;case 2:mainline=tr("mDHT: error");break;case 3:mainline=tr("mDHT: closed");break;default:break;}
  mainlineTip=tr("Mainline DHT\nLookups: %1\nAnnounces: %2\nDelivered peers: %3\nThese are service counters, not estimated network size").arg(runtime_->mainlineLookups).arg(runtime_->mainlineAnnounces).arg(runtime_->mainlinePeers);
 }
 mainline_->setText(mainline);mainline_->setToolTip(tip(mainlineTip));
 QString ratio=QStringLiteral("—");if(ratioKnown_&&!offline_){ratio=downloaded_?QLocale().toString(static_cast<double>(static_cast<long double>(uploaded_)/downloaded_),'f',3):uploaded_?QStringLiteral("∞"):QStringLiteral("0.000");}ratio_->setText(tr("Ratio: %1").arg(ratio));ratio_->setToolTip(tip(tr("Overall upload/download ratio\nClick to open transfer statistics")));
 const auto incoming=metric("net.has_incoming_connections");nat_->setText(incoming&&*incoming?tr("NAT: incoming"):tr("NAT: unknown"));nat_->setToolTip(tip(incoming&&*incoming?tr("An incoming connection has been observed in this session. This is not an active port test."):tr("Incoming reachability has not been established; no active port test is performed.")));
 ips_->setText(tr("IPs: —"));QString filterTip=tr("Original IP-filter range / blocked / banned / bad-IP counts are unavailable from this runtime. SDK disconnect counters are not unique IP counts.");
 if(const auto banned=metric("peer.num_banned_peers")){filterTip+=tr("\nSDK banned-peer disconnect events: %1").arg(*banned);}if(const auto bad=metric("peer.banned_for_hash_failure")){filterTip+=tr("\nSDK hash-failure ban events: %1").arg(*bad);}ips_->setToolTip(tip(filterTip));
 QStringList info;QString address;
 if(!offline_&&runtime_&&runtime_->current&&runtime_->network&&runtime_->network->current){for(const auto& family:runtime_->network->families){if(family.address.isEmpty()){continue;}if(address.isEmpty()){address=family.address;}auto line=family.address;if(family.asn){line+=" — AS"+family.asn->number+' '+family.asn->name+" ("+family.asn->prefix+')';}else if(!family.status.isEmpty()){line+=" — "+family.status;}info.push_back(line);}}
 network_->setText(address.isEmpty()?tr("IP: —"):tr("IP: %1").arg(fontMetrics().elidedText(address,Qt::ElideMiddle,145)));network_->setToolTip(tip(info.isEmpty()?tr("Current public network information is unavailable"):info.join('\n')));visibility();
}
}
