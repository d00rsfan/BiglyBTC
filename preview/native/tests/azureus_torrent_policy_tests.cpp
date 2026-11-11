// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_torrent.hpp"
#include "original_torrent_identity_lt.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace {
using namespace biglybtc;namespace lt=libtorrent;namespace fs=std::filesystem;
std::size_t assertions=0;
void require(bool okay,const char* message){++assertions;if(!okay){throw std::runtime_error(message);}}
std::string encode(const lt::entry& value){std::string out;lt::bencode(std::back_inserter(out),value);return out;}
void write(const fs::path& path,const lt::entry& value){fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary|std::ios::trunc);stream<<encode(value);stream.flush();require(bool(stream),"Generated document write");}
struct Fixture {
 fs::path root;lt::entry state{lt::entry::dictionary_type{}},config{lt::entry::dictionary_type{}},inventory{lt::entry::dictionary_type{}};
 std::optional<engine::AzureusTorrentTarget> target;std::optional<storage::ProfileLease> lease;
 Fixture(){char pattern[]="/tmp/biglybtc-azureus-policy-XXXXXX";const auto made=::mkdtemp(pattern);require(made,"Generated policy profile");root=made;fs::create_directory(root/"active");require(::chmod(root.c_str(),0700)==0,"Owned mode");
  state["info"]["name"]="generated.bin";state["info"]["length"]=3;state["info"]["piece length"]=16384;state["info"]["pieces"]=lt::hasher("abc",3).final().to_string();
  state["announce"]="https://tracker.invalid/announce?passkey=generated-secret";state["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public")};state["attributes"]["peersources"]=lt::entry::list_type{lt::entry("DHT")};
  state["attributes"]["pluginoptions"]["mldht"]["enableannounce"]=0;state["azureus_properties"]["dht_backup_enable"]=1;
  config["TCP.Listen.Port"]=44444;config["UDP.Listen.Port"]=44444;config["Plugin.DHT.dht.enabled"]=1;config["Plugin.mlDHT.enable"]=0;
  save(true);auto held=storage::ProfileLease::acquire(root);require(bool(held),"Generated lease");lease=std::move(*held);
 }
 ~Fixture(){lease.reset();std::error_code ec;fs::remove_all(root,ec);}
 void save(bool newIdentity=false){
  if(newIdentity||!target){lt::error_code ec;const auto bytes=encode(state);lt::torrent_info info(lt::span<const char>(bytes.data(),bytes.size()),ec,lt::from_span);require(!ec,"Generated metadata");auto doc=core::Document::parse(bytes);require(bool(doc),"Generated source");auto identity=storage::originalTorrentIdentityFromLibtorrent(*doc,info);require(bool(identity),"Generated full identity");target.emplace(engine::AzureusTorrentTarget{identity->originalKey,identity->full,17,44444,44444,false,false,false,{},31,std::make_shared<std::atomic<bool>>(true)});
   lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=std::string(reinterpret_cast<const char*>(target->hash.bytes().data()),target->hash.bytes().size());row["torrent"]=(root/"active"/(target->hash.hex()+".dat")).string();row["save_dir"]=(root/"payload").string();row["save_file"]="generated.bin";row["state"]=0;row["persistent"]=1;inventory["downloads"]=lt::entry::list_type{row};
  }
  write(root/"biglybt.config",config);write(root/"downloads.config",inventory);write(root/"active"/(target->hash.hex()+".dat"),state);
 }
};
void codec(){core::SharedUdpEndpoint origin{{8,8,8,8},6881,4};const auto encoded=engine::encodeAzureusPeer(51413,51414,6881,true);require(encoded=="51413;C;51414","Original tracker encoding");auto peer=engine::decodeAzureusPeer(encoded,1,origin);require(peer&&peer->tcp.port==51413&&peer->udpPort==51414&&peer->crypto&&!peer->seeding,"Original downloading value");
 peer=engine::decodeAzureusPeer(" 51413 ;C; 51414 ",2,origin);require(peer&&peer->seeding,"Original trim and seed flag");
 require(engine::encodeAzureusPeer(51413,6881,6881,false)=="51413","No redundant UDP field");
 for(const auto* text:{"https://tracker.invalid/?passkey=generated-secret","host.invalid:80","0","65536","-1","51413;65536","1:2:3","51413\n","10.0.0.1:6881","127.0.0.1:6881"}){require(!engine::decodeAzureusPeer(text,1,origin),"Malformed/nonpublic/URL value rejected");}
 require(!engine::decodeAzureusPeer("51413",0x10,origin),"Chat/rating flags never become torrent peers");
 origin.address={};origin.address[0]=0x20;origin.address[1]=1;origin.address[2]=0x48;origin.address[3]=0x60;origin.length=16;peer=engine::decodeAzureusPeer("51413",2,origin);require(peer&&peer->tcp.length==16&&peer->udpPort==6881,"IPv6 originator retains numeric address");
 origin={{127,23,1,2},6881,4};require(engine::decodeAzureusPeer("51413",1,origin,true).has_value(),"Explicit owned scope");require(!engine::decodeAzureusPeer("51413",1,origin),"Owned peer cannot escape into Public scope");
}
void policy(){Fixture f;auto made=engine::AzureusTorrentSources::create(*f.lease);require(bool(made),"Source owner");auto& source=**made;auto allowed=[&]{auto value=source.check(*f.target);require(bool(value),"Source check");return *value;};
 require(allowed(),"Public DHT independent of mlDHT disable");const auto parses=source.parses();for(int i=0;i<100;++i){require(allowed(),"Stable source reused");}require(source.parses()==parses,"No repeated unchanged document parsing");
 auto wrong=*f.target;wrong.full.v1->front()^=std::byte{1};require(source.check(wrong)==false,"Memo remains bound to complete identity");
 f.state["attributes"]["peersources"]=lt::entry::list_type{lt::entry("Tracker")};f.save();require(!allowed(),"Disabled DHT source");
 f.state["attributes"]["peersources"]=lt::entry::list_type{lt::entry("DHT")};f.state["attributes"]["peersourcesdenied"]=lt::entry::list_type{lt::entry("DHT")};f.save();require(!allowed(),"Denied DHT source");f.state["attributes"].dict().erase("peersourcesdenied");
 f.state["attributes"]["networks"]=lt::entry::list_type{lt::entry("I2P")};f.save();require(!allowed(),"No Public permission");f.state["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public")};
 f.state["azureus_properties"]["dht_backup_enable"]=0;f.save();require(!allowed(),"Backup disabled");
 f.state["announce"]="dht://generated.dht/announce";f.state["attributes"]["peersources"]=lt::entry::list_type{};f.save();require(allowed(),"Java trackerless peer-source exception");
 f.state["announce"]="https://tracker.invalid/announce?passkey=generated-secret";f.state["azureus_properties"]["dht_backup_enable"]=1;f.state["attributes"]["peersources"]=lt::entry::list_type{lt::entry("DHT")};f.save();f.target->trackerHealthy=true;require(!allowed(),"Healthy tracker without measured limited selection");f.target->swarmSize=10;require(allowed(),"Small healthy swarm limited tracking");f.target->swarmSize=1000;f.target->selection=100;require(!allowed(),"Stable large swarm sampling");
 f.state["azureus_properties"]["dht_backup_requested"]=1;f.save();require(allowed(),"Explicit torrent backup request");f.state["azureus_properties"].dict().erase("dht_backup_requested");f.config["Plugin.DHT Tracker.dhttracker.tracknormalwhenoffline"]=0;f.save();require(allowed(),"Original always-track option");
 f.target->live->store(false);require(!allowed(),"Revoked admission");require(source.check(*f.target,true)==true,"Previously public withdrawal remains possible");f.target->live->store(true);
 f.inventory["downloads"].list().front()["state"]=70;f.save();require(!allowed(),"Stopped torrent cannot publish");require(source.check(*f.target,true)==true,"Stop permits only separately typed withdrawal");
 f.inventory["downloads"].list().front()["state"]=0;f.config["Plugin.DHT.dht.enabled"]=0;f.save();require(!allowed(),"Global DHT disabled");f.config["Plugin.DHT.dht.enabled"]=1;
 f.state["info"]["private"]=1;f.save(true);require(!allowed(),"Actual private identity refused before lookup/publication");require(source.check(*f.target,true)==false,"Private source cannot authorise withdrawal disclosure");
 require(!fs::exists(f.root/"payload"),"No payload opened or created by source checks");
}
}
int main(){try{codec();policy();std::cout<<"PASS Azureus peer codec/source policy: "<<assertions<<" assertions; unchanged parses memoised; private/passkey source withheld\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
