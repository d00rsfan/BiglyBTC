// SPDX-License-Identifier: GPL-2.0-or-later
#include "swarm_discovery_controller.hpp"
namespace biglybtc::app {
SwarmDiscoveryController::SwarmDiscoveryController(QObject* parent):QObject(parent){timer_.setInterval(250);connect(&timer_,&QTimer::timeout,this,&SwarmDiscoveryController::poll);timer_.start();}
void SwarmDiscoveryController::setService(std::shared_ptr<GeneralChats> service){if(service_==service){return;}service_=std::move(service);if(service_){generation_=0;sequence_=0;poll();}else{ui::SwarmViewSnapshot out;out.generation=generation_;out.status=tr("Open a profile to use Swarm Discoveries");out.sequence=++sequence_;emit snapshotReady(std::move(out));}}
void SwarmDiscoveryController::accepted(bool ok){if(!ok){emit notice(tr("Discoveries are unavailable or their command queue is full"));}}
void SwarmDiscoveryController::refresh(){accepted(service_&&service_->refreshDiscoveries());}
void SwarmDiscoveryController::discover(){accepted(service_&&service_->discoverNow());}
void SwarmDiscoveryController::lookup(QByteArray hash){accepted(service_&&service_->lookupDiscoveries(hash.toStdString()));}
void SwarmDiscoveryController::submit(SwarmDiscoveryChanges changes){accepted(service_&&service_->changeDiscoveries(std::move(changes)));}
void SwarmDiscoveryController::settings(services::related_content::Settings value){SwarmDiscoveryChanges changes;changes.settings=std::move(value);submit(std::move(changes));}
void SwarmDiscoveryController::read(QStringList keys,bool unread){SwarmDiscoveryChanges changes;for(const auto& key:keys){(unread?changes.unread:changes.read).push_back(key.toStdString());}submit(std::move(changes));}
void SwarmDiscoveryController::remove(QStringList keys){SwarmDiscoveryChanges changes;for(const auto& key:keys){changes.removed.push_back(key.toStdString());}submit(std::move(changes));}
void SwarmDiscoveryController::resetDeleted(){SwarmDiscoveryChanges changes;changes.clearDeleted=true;submit(std::move(changes));}
void SwarmDiscoveryController::layout(QByteArray value){SwarmDiscoveryChanges changes;changes.tableLayout=value.toStdString();submit(std::move(changes));}
void SwarmDiscoveryController::poll(){if(!service_){return;}const auto value=service_->snapshot();if(!value||(value->generation==generation_&&value->sequence==sequence_)){return;}generation_=value->generation;sequence_=value->sequence;const auto& network=value->discoveryNetwork;ui::SwarmViewSnapshot out;out.generation=generation_;out.sequence=sequence_;out.profile=value->discoveries;out.status=QString::fromStdString(network.status.empty()?value->availability:network.status);out.lookups=network.lookups;out.publications=network.publications;out.completed=network.completed;out.active=network.active;out.pending=network.pending;emit snapshotReady(std::move(out));}
}
