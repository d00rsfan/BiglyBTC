// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/bencode.hpp"
#include "subscription_seed_owner.hpp"

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_status.hpp>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
namespace lt=libtorrent;
namespace engine=biglybtc::engine;
using namespace std::chrono_literals;

void require(bool condition,const char* message) {
  if(!condition) { throw std::runtime_error(message); }
}

std::string_view field(const biglybtc::core::Node& node,std::string_view key) {
  const auto* value=node.find(key);
  require(value&&value->type==biglybtc::core::Node::Type::Bytes,"Missing Java fixture field");
  return value->bytes;
}

template<class Predicate> bool waitFor(const Predicate& predicate) {
  const auto deadline=std::chrono::steady_clock::now()+5s;
  while(!predicate()) {
    if(std::chrono::steady_clock::now()>=deadline) { return false; }
    std::this_thread::sleep_for(20ms);
  }
  return true;
}

void handshake(QTcpSocket& socket,unsigned short port,const engine::SubscriptionSeedHash& hash) {
  socket.connectToHost(QHostAddress::LocalHost,port);
  require(socket.waitForConnected(3000),"Connect to the test-owned listener");
  QByteArray request("\x13" "BitTorrent protocol",20);
  request.append(QByteArray(8,'\0'));
  request.append(hash.data(),hash.size());
  request.append("-TEST00-abcdefghijkl",20);
  require(socket.write(request)==request.size()&&socket.flush(),"Write peer handshake");
}

enum class Source { Valid, Corrupt, Revoked };

void check(const biglybtc::core::Node& fixture,Source mode) {
  QTemporaryDir temporary;
  require(temporary.isValid(),"Create the fixture's isolated directory");
  const auto directory=std::filesystem::path(temporary.path().toStdU16String());
  const auto file=directory/std::string(field(fixture,"file"));
  auto payload=std::string(field(fixture,"payload"));
  require(!payload.empty(),"Nonempty fixture payload");
  if(mode==Source::Corrupt) { payload.back()^=1; }
  {
    std::ofstream out(file,std::ios::binary);
    out.write(payload.data(),static_cast<std::streamsize>(payload.size()));
    require(bool(out),"Write fixture payload");
  }
  engine::SubscriptionSeedHash hash{};
  const auto identity=field(fixture,"hash");
  require(identity.size()==hash.size(),"20-byte signed publication hash");
  std::ranges::copy(identity,hash.begin());
  const engine::SubscriptionSeedSource source{std::string(field(fixture,"metainfo")),directory};
  unsigned prepared=0,revalidated=0;
  const engine::SubscriptionSeedResolver resolve=[&](std::string_view key,engine::SubscriptionSeedRead read)
      ->std::optional<engine::SubscriptionSeedSource> {
    require(key==identity,"Resolve only the watched subscription");
    if(read==engine::SubscriptionSeedRead::Prepare) { ++prepared; }
    else { ++revalidated; if(mode==Source::Revoked) { return {}; } }
    return source;
  };

  lt::settings_pack settings;
  settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");
  settings.set_bool(lt::settings_pack::enable_dht,false);
  settings.set_bool(lt::settings_pack::enable_lsd,false);
  settings.set_bool(lt::settings_pack::enable_upnp,false);
  settings.set_bool(lt::settings_pack::enable_natpmp,false);
  lt::session session(settings);
  engine::detail::SubscriptionSeedOwner owner;
  owner.refresh(session,std::span{&hash,1},resolve);
  require(waitFor([&]{return session.is_listening()&&session.listen_port()!=0;}),"Bind isolated loopback listener");
  require(session.get_torrents().empty()&&prepared==0,"Watching needs no live torrent or payload read");

  QTcpSocket first;
  handshake(first,session.listen_port(),hash);
  if(first.state()!=QAbstractSocket::UnconnectedState) {
    require(first.waitForDisconnected(3000),"First handshake closes while the owner checks the payload");
  }
  require(first.bytesAvailable()==0,"No seed handshake before checking the payload");

  require(waitFor([&]{
    owner.refresh(session,std::span{&hash,1},resolve);
    const auto handles=session.get_torrents();
    if(mode!=Source::Valid) { return prepared==1&&handles.empty(); }
    if(handles.size()!=1||revalidated!=1) { return false; }
    const auto status=handles.front().status({});
    return status.is_seeding&&!(status.flags & lt::torrent_flags::paused);
  }),"Complete full verification or withdraw an invalid seed");
  require(prepared==1&&revalidated==static_cast<unsigned>(mode!=Source::Corrupt),"Revalidate only a fully checked payload");

  if(mode==Source::Valid) {
    QTcpSocket peer;
    handshake(peer,session.listen_port(),hash);
    while(peer.bytesAvailable()<68&&peer.waitForReadyRead(3000)) {}
    require(peer.read(68).mid(28,20)==QByteArray(hash.data(),hash.size()),"Retry uses Java's signed peer-wire identity");
  }
  owner.clear(session);
  require(!owner.listening()&&session.get_torrents().empty(),"Withdraw the ephemeral seed");
  std::ifstream finalFile(file,std::ios::binary);
  require(std::string(std::istreambuf_iterator<char>(finalFile),{})==payload,"Retirement preserves the versioned payload");
}
} // namespace

int main(int argc,char** argv) try {
  QCoreApplication application(argc,argv);
  if(argc!=2) { return 2; }
  if(!engine::detail::SubscriptionSeedOwner::supported()) { return 77; }
  std::ifstream input(argv[1],std::ios::binary);
  const auto fixtures=biglybtc::core::Document::parse({std::istreambuf_iterator<char>(input),{}});
  require(fixtures&&fixtures->find("new"),"Load Java subscription fixture");
  for(const auto source:{Source::Valid,Source::Corrupt,Source::Revoked}) { check(*fixtures->find("new"),source); }
  std::cout<<"Subscription seed verification, peer retry and withdrawal passed\n";
  return 0;
} catch(const std::exception& error) {
  std::cerr<<error.what()<<'\n';
  return 1;
}
