// SPDX-License-Identifier: GPL-2.0-or-later
#include "network_identity_controller.hpp"
#include <QDnsLookup>
#include <algorithm>
namespace biglybtc::app {
namespace ni=services::network_identity;
NetworkIdentityController::NetworkIdentityController(QObject* parent,QHostAddress resolver,quint16 port,int timeout):QObject(parent),resolver_(std::move(resolver)),port_(port),timeout_(timeout){for(std::size_t i=0;i<slots_.size();++i){auto& slot=slots_[i];slot.timer=new QTimer(this);slot.timer->setSingleShot(true);connect(slot.timer,&QTimer::timeout,this,[this,i]{auto& s=slots_[i];cancel(s);s.value.status=tr("ASN lookup timed out");s.retry=QDateTime::currentMSecsSinceEpoch()+60000;publish();});}}
NetworkIdentityController::~NetworkIdentityController(){for(auto& s:slots_){cancel(s);}}
void NetworkIdentityController::cancel(Slot& s){s.timer->stop();if(s.lookup){auto* lookup=s.lookup.data();s.lookup=nullptr;lookup->disconnect(this);lookup->abort();lookup->deleteLater();}}
void NetworkIdentityController::beginSession(quint64 session){if(session_==session){return;}for(auto& s:slots_){cancel(s);s.value={};s.retry=0;}session_=session;epoch_=0;sourceSequence_=0;sequence_=0;latest_.reset();cache_.clear();}
void NetworkIdentityController::observe(ui::RuntimeMetricsPresentation packet){
 if(!packet||!session_||packet->session!=session_||!packet->epoch||!packet->sequence||packet->epoch<epoch_||(packet->epoch==epoch_&&packet->sequence<=sourceSequence_)){return;}
 if(epoch_!=packet->epoch){for(auto& s:slots_){cancel(s);s.value={};s.retry=0;}epoch_=packet->epoch;sequence_=0;}sourceSequence_=packet->sequence;latest_=std::move(packet);const auto now=QDateTime::currentMSecsSinceEpoch();std::erase_if(cache_,[now](const auto& c){return c.until<now;});
 for(std::size_t i=0;i<slots_.size();++i){auto& s=slots_[i];const auto input=latest_->networkInput;QHostAddress address(input?input->addresses[i]:QString{});if(!latest_->current||!ni::publicAddress(address)||((i==0)!=(address.protocol()==QAbstractSocket::IPv4Protocol))){address.clear();}const auto text=address.isNull()?QString{}:address.toString();if(text!=s.value.address){cancel(s);s.value={};s.value.address=text;s.retry=0;}
  if(text.isEmpty()){s.value.status=latest_->current?tr("Not detected yet"):tr("Session ended");continue;}
  if(!input->dnsAllowed){cancel(s);s.value.status=tr("ASN DNS lookup disabled by the current network policy");}
  if(s.value.asn){if(s.value.asn->name.isEmpty()&&input->dnsAllowed&&!s.lookup&&now>=s.retry){s.name=true;start(i,"AS"+s.value.asn->number+".asn.cymru.com");}continue;}
  auto existing=std::ranges::find_if(input->saved,[&](const auto& row){return ni::matches(row,address);});if(existing!=input->saved.end()){s.value.asn=*existing;s.value.cached=true;s.value.status=tr("Saved ASN cache; matching current address");continue;}
  const auto cached=std::ranges::find_if(cache_,[&](const auto& c){return ni::matches(c.value,address);});if(cached!=cache_.end()){s.value.asn=cached->value;s.value.cached=true;s.value.status=tr("Cached ASN lookup");continue;}
  if(input->dnsAllowed&&!s.lookup&&now>=s.retry){s.name=false;s.value.status=tr("Looking up network operator…");start(i,ni::originQuery(address));}
 }publish();
}
void NetworkIdentityController::start(std::size_t i,QString name){auto& s=slots_[i];cancel(s);auto* lookup=new QDnsLookup(QDnsLookup::TXT,name,this);s.lookup=lookup;if(!resolver_.isNull()){lookup->setNameserver(resolver_,port_);}connect(lookup,&QDnsLookup::finished,this,[this,i]{finished(i);});s.timer->start(timeout_);lookup->lookup();}
void NetworkIdentityController::finished(std::size_t i){auto& s=slots_[i];if(!s.lookup){return;}auto* lookup=s.lookup.data();s.timer->stop();const bool ok=lookup->error()==QDnsLookup::NoError;std::vector<QString> records;if(ok){for(const auto& record:lookup->textRecords()){QByteArray data;for(const auto& part:record.values()){if(data.size()+part.size()>4096){data.clear();break;}data+=part;}if(!data.isEmpty()){records.push_back(QString::fromUtf8(data));}if(records.size()==16){break;}}}cancel(s);
 if(!s.name){for(const auto& record:records){if(auto parsed=ni::parseOrigin(QHostAddress(s.value.address),record)){s.value.asn=std::move(parsed);s.name=true;start(i,"AS"+s.value.asn->number+".asn.cymru.com");return;}}}
 else if(s.value.asn){for(const auto& record:records){if(auto name=ni::parseName(s.value.asn->number,record)){s.value.asn->name=std::move(*name);s.value.status=tr("DNS ASN lookup");s.value.cached=false;cache_.push_back({*s.value.asn,QDateTime::currentMSecsSinceEpoch()+86400000});if(cache_.size()>16){cache_.erase(cache_.begin());}publish();return;}}}
 s.value.status=tr("ASN lookup unavailable");s.retry=QDateTime::currentMSecsSinceEpoch()+60000;publish();
}
void NetworkIdentityController::publish(){if(!latest_||sequence_==UINT64_MAX){return;}auto packet=std::make_shared<ui::RuntimeMetricsPacket>(*latest_);auto network=std::make_shared<ni::Snapshot>();network->current=latest_->current;for(std::size_t i=0;i<slots_.size();++i){network->families[i]=slots_[i].value;}packet->network=std::move(network);packet->sequence=++sequence_;emit ready(std::move(packet));}
}
