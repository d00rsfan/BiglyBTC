#pragma once
#include "scheduler_host_fixture.hpp"
#include "owned_engine_fixture.hpp"
#include "library_commands.hpp"
#include "ownership_file.hpp"
namespace biglybtc::test::imported_scheduler {
namespace sh=biglybtc::test::scheduler_host;
inline void prepare(test::OwnedEngineFixture& f,bool enabled,bool forced=false){
  sh::original(f.profile);
  libtorrent::entry cfg(libtorrent::entry::dictionary_type{});
  cfg["Bind IP"]="127.0.0.1";cfg["TCP.Listen.Port.Enable"]=0;cfg["peercontrol.udp.public.enable"]=0;
  cfg["Peer Source Selection Default.Tracker"]=0;cfg["Peer Source Selection Default.DHT"]=0;
  cfg["Peer Source Selection Default.PeerExchange"]=0;cfg["Plugin.DHT.dht.enabled"]=0;
  cfg["Plugin.SpeedScheduler.speedscheduler.enabled"]=enabled?1:0;cfg["Plugin.SpeedScheduler.thread.sleep.time"]=1000;
  cfg["Max Upload Speed KBs"]=7;cfg["Max Download Speed KBs"]=9;
  cfg["Resume Downloads On Start"]=0;cfg["opaque.imported.fixture"]="preserved";
  test::write(f.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(cfg)));
  auto state=sh::document(test::read(f.stateFile));const auto* attrs=state.find("attributes");sh::require(attrs,"State attributes");
  auto values=sh::document(QByteArray::fromStdString(state.originalBytes().substr(attrs->begin,attrs->end-attrs->begin)));
  sh::require(bool(values.replaceValue("parameters","d8:dndflagsi0ee")),"Original no-DND cache flags");
  sh::require(bool(values.replaceValue("skipfss","i0e")),"Original selected-size cache");
  sh::require(bool(state.replaceValue("attributes",values.serialize())),"Retain full state");
  test::write(f.stateFile,QByteArray::fromStdString(state.serialize()));
  auto held=storage::ProfileLease::acquire(f.native());sh::require(bool(held),"Generated lease");
  if(forced){sh::require(bool(storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::ForceStart)),"Saved forced intent");}
  sh::require(bool(storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::Pause)),"Saved original pause");
  const auto bytes=f.hash->bytes();auto encoded=storage::scheduler::originalOwnershipHash(
      {reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});sh::require(bool(encoded),"Source ownership format");
  sh::write(sh::schedulerFile(f.profile,"PausedTorrents.conf"),QByteArray::fromStdString(*encoded+"\r\nopaque survivor\r\n"));
}
 } // namespace biglybtc::test::imported_scheduler
