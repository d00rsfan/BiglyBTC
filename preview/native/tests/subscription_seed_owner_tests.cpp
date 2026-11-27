// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/bencode.hpp"
#include "subscription_seed_owner.hpp"
#include "metadata_disk_router.hpp"

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_status.hpp>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
namespace state=biglybtc::storage::detail;
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

QByteArray readExact(QTcpSocket& socket,qsizetype count) {
  while(socket.bytesAvailable()<count) {
    require(socket.waitForReadyRead(3000),"Receive the expected peer bytes");
  }
  return socket.read(count);
}

QByteArray peerMessage(QTcpSocket& socket) {
  const auto prefix=readExact(socket,4);
  const auto size=qFromBigEndian<quint32>(prefix.constData());
  require(size<=16*1024+9,"Bound the peer message");
  return readExact(socket,static_cast<qsizetype>(size));
}

enum class Source { Valid, Corrupt, Revoked, Moved, MissingParent };

void check(const biglybtc::core::Node& fixture,Source mode) {
  QTemporaryDir temporary;
  require(temporary.isValid(),"Create the fixture's isolated directory");
  auto lease=biglybtc::storage::ProfileLease::acquire(std::filesystem::path(temporary.path().toStdU16String()));
  require(lease.has_value(),"Hold the subscription test profile");
  auto parent=state::duplicateStateDirectory(lease->directoryHandle());
  require(parent.has_value(),"Retain the owned subscriptions directory");
  const auto retained=state::stateDirectoryPath(parent->get());
  require(retained.has_value(),"Resolve the held subscriptions directory");
  const auto directory=*retained;
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
  const engine::SubscriptionSeedSource source{std::string(field(fixture,"metainfo")),directory,
      std::make_shared<const state::StateDescriptor>(std::move(*parent))};
  unsigned prepared=0,revalidated=0;
  const engine::SubscriptionSeedResolver resolve=[&](std::string_view key,engine::SubscriptionSeedRead read)
      ->std::optional<engine::SubscriptionSeedSource> {
    require(key==identity,"Resolve only the watched subscription");
    if(read==engine::SubscriptionSeedRead::Prepare) {
      ++prepared;
      if(mode==Source::MissingParent) { return engine::SubscriptionSeedSource{source.metainfo,directory}; }
      return source;
    }
    ++revalidated;
    if(mode==Source::Revoked) { return {}; }
    if(mode==Source::Moved) { return engine::SubscriptionSeedSource{source.metainfo,directory/"other"}; }
    return engine::SubscriptionSeedSource{source.metainfo,directory};
  };

  lt::settings_pack settings;
  settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");
  settings.set_bool(lt::settings_pack::enable_dht,false);
  settings.set_bool(lt::settings_pack::enable_lsd,false);
  settings.set_bool(lt::settings_pack::enable_upnp,false);
  settings.set_bool(lt::settings_pack::enable_natpmp,false);
  auto disks=std::make_shared<engine::detail::MetadataDiskPolicy>();
  lt::session_params sessionParams(settings);
  sessionParams.disk_io_constructor=engine::detail::metadataDiskConstructor(disks);
  lt::session session(std::move(sessionParams));
  engine::detail::SubscriptionSeedOwner owner;
  owner.refresh(session,*disks,std::span{&hash,1},resolve);
  require(waitFor([&]{return session.is_listening()&&session.listen_port()!=0;}),"Bind isolated loopback listener");
  require(session.get_torrents().empty()&&prepared==0,"Watching needs no live torrent or payload read");

  QTcpSocket first;
  handshake(first,session.listen_port(),hash);
  if(first.state()!=QAbstractSocket::UnconnectedState) {
    require(first.waitForDisconnected(3000),"First handshake closes while the owner checks the payload");
  }
  require(first.bytesAvailable()==0,"No seed handshake before checking the payload");

  require(waitFor([&]{
    owner.refresh(session,*disks,std::span{&hash,1},resolve);
    const auto handles=session.get_torrents();
    if(mode!=Source::Valid) { return prepared==1&&handles.empty(); }
    if(handles.size()!=1||revalidated!=1) { return false; }
    const auto status=handles.front().status({});
    return status.is_seeding&&!(status.flags & lt::torrent_flags::paused);
  }),"Complete full verification or withdraw an invalid seed");
  const bool checked=mode!=Source::Corrupt&&mode!=Source::MissingParent;
  require(prepared==1&&revalidated==static_cast<unsigned>(checked),"Revalidate only a fully checked payload");

  if(mode==Source::Valid) {
    QTcpSocket peer;
    handshake(peer,session.listen_port(),hash);
    require(readExact(peer,68).mid(28,20)==QByteArray(hash.data(),hash.size()),
        "Retry uses Java's signed peer-wire identity");
    const QByteArray interested=QByteArray::fromHex("0000000102");
    require(peer.write(interested)==interested.size()&&peer.flush(),"Indicate peer interest");
    auto message=peerMessage(peer);
    for(int n=0;message!=QByteArray(1,'\x01')&&n<8;++n) { message=peerMessage(peer); }
    require(message==QByteArray(1,'\x01'),"The verified seed unchokes the requesting peer");
    QByteArray request=QByteArray::fromHex("0000000d06000000000000000000000000");
    const auto length=static_cast<quint32>(payload.size());
    qToBigEndian(length,request.data()+13);
    require(peer.write(request)==request.size()&&peer.flush(),"Request the verified subscription piece");
    message=peerMessage(peer);
    for(int n=0;(message.isEmpty()||message.front()!='\x07')&&n<8;++n) { message=peerMessage(peer); }
    require(message.mid(9)==QByteArray::fromStdString(payload),"Read the original payload through the read-only disk route");
    require(!std::filesystem::exists(directory/".BiglyBTC-subscription-seed-1"),"Keep virtual save paths off disk");
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
  for(const auto source:{Source::Valid,Source::Corrupt,Source::Revoked,Source::Moved,Source::MissingParent}) {
    check(*fixtures->find("new"),source);
  }
  std::cout<<"Subscription seed verification, peer retry and withdrawal passed\n";
  return 0;
} catch(const std::exception& error) {
  std::cerr<<error.what()<<'\n';
  return 1;
}
