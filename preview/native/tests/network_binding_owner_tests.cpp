#include "network_binding.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#if __has_include(<libtorrent/biglybtc_swarm.hpp>)
#include "selected_swarm_fixture.hpp"
#include "../engine/managed_payload_counters.hpp"
#include <libtorrent/address.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/biglybtc_swarm.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/pex_flags.hpp>
#include <algorithm>
#include <set>
#include <sstream>
#include <string_view>
#endif
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <ifaddrs.h>
#include <libtorrent/torrent_status.hpp>
#include <chrono>
#include <cerrno>
#include <memory>
namespace {
using namespace biglybtc;
namespace lt=libtorrent;
using namespace std::chrono_literals;
thread_local bool failEnumeration=false;
thread_local unsigned failedEnumerations=0;
struct FailEnumeration {
  FailEnumeration(){failedEnumerations=0;failEnumeration=true;}
  ~FailEnumeration(){failEnumeration=false;}
};
engine::NetworkPolicy localPolicy(bool ipv6,bool utp){
  engine::NetworkPolicy p;p.mode=engine::NetworkMode::Normal;p.ipv6=ipv6;
  p.bindAddresses={ipv6?"lo[v6]":"lo[v4]"};p.enforceBind=true;p.checkBindOnStart=false;
  p.incomingTcp=p.outgoingTcp=!utp;p.incomingUtp=p.outgoingUtp=utp;
  return p; // Explicit loopback,port0; all discovery/proxy/public sources absent.
}
void tracker(test::OwnedEngineFixture& fixture,const QString& url){
  auto doc=core::Document::parse(test::read(fixture.stateFile).toStdString());test::require(bool(doc),"generated state");
  const auto text=url.toUtf8();test::require(doc->replaceValue("announce",std::to_string(text.size())+":"+text.toStdString()).has_value(),"owned tracker source");
  test::write(fixture.stateFile,QByteArray::fromStdString(doc->serialize()));
}
QByteArray compactPeer(bool ipv6,unsigned short port){
  QByteArray bytes(ipv6?16:4,'\0');if(ipv6){bytes[15]='\1';}else{bytes[0]='\x7f';bytes[3]='\1';}
  bytes+=static_cast<char>(port>>8);bytes+=static_cast<char>(port);return bytes;
}
#if __has_include(<libtorrent/biglybtc_swarm.hpp>)
namespace selectedFixture = test::selected_swarm;
// The exact qualified provider is required before creating any new session.
// Old SDKs retain their existing route tests; no selected-uTP row is skipped.
lt::settings_pack selectedUtpSettings(bool ipv6) {
  auto binding=engine::prepareNetworkBinding(localPolicy(ipv6,true));
  test::require(binding.has_value(),"Selected-uTP requires the actual loopback family");
  auto settings=engine::makeNetworkSettings(binding->effective);
  test::require(settings.has_value(),"Selected-uTP settings must validate");
  settings->set_int(lt::settings_pack::in_enc_policy,lt::settings_pack::pe_disabled);
  settings->set_int(lt::settings_pack::out_enc_policy,lt::settings_pack::pe_disabled);
  settings->set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::error
      |lt::alert_category::status|lt::alert_category::storage|lt::alert_category::connect
      |lt::alert_category::peer|lt::alert_category::peer_log));
  settings->set_int(lt::settings_pack::alert_queue_size,20000);
  settings->set_int(lt::settings_pack::unchoke_interval,1);
  return *settings;
}
std::shared_ptr<lt::torrent_info> selectedUtpMetadata(const selectedFixture::Fixture& fixture) {
  lt::error_code error;
  auto info=std::make_shared<lt::torrent_info>(lt::span<const char>(fixture.source.data(),fixture.source.size()),error,lt::from_span);
  test::require(!error&&info->info_hashes().has_v1()&&info->info_hashes().has_v2(),"Actual generated hybrid required");
  test::require(std::string(info->info_section().data(),info->info_section().size())==fixture.rawInfo,"Raw info identity changed");
  info->clear_trackers();return info;
}
lt::add_torrent_params selectedUtpParameters(const std::shared_ptr<lt::torrent_info>& info,
    const std::filesystem::path& directory) {
  lt::add_torrent_params parameters;parameters.ti=info;parameters.save_path=directory.string();
  parameters.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);
  parameters.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
  return parameters;
}
std::string selectedUtpHashText(const lt::sha1_hash& hash) {
  return QByteArray(hash.data(),hash.size()).toHex().toStdString();
}
struct SelectedUtpEvidence {
  bool dropped=false,wrongRoute=false,payload=false;
  std::set<std::string> handshakes,errors;
  std::size_t peerCount=0;std::int64_t maximumPeerSent=0,maximumPeerReceived=0;
  void error(std::string value){if(errors.size()<16){errors.insert(value.substr(0,256));}}
  std::string diagnostic() const {
    std::ostringstream text;text<<"payload="<<payload<<" dropped="<<dropped<<" wrongRoute="<<wrongRoute
        <<" peers="<<peerCount<<" maxPeerSent="<<maximumPeerSent<<" maxPeerReceived="<<maximumPeerReceived<<" hashes=[";
    for(const auto& hash:handshakes){text<<hash<<',';}text<<"] errors=[";
    for(const auto& value:errors){text<<value<<"; ";}text<<"] incomingUtp="<<incomingUtp.size()<<" invalidHash="<<invalidHash.size();
    return text.str();
  }
  std::set<lt::tcp::endpoint> incomingUtp,invalidHash;
  void pop(lt::session& session,const lt::address& address) {
    std::vector<lt::alert*> alerts;session.pop_alerts(&alerts);
    for(const auto* alert:alerts) {
      if(lt::alert_cast<lt::alerts_dropped_alert>(alert)){dropped=true;}
      if(const auto* event=lt::alert_cast<lt::incoming_connection_alert>(alert)) {
        if(event->socket_type!=lt::socket_type_t::utp||event->endpoint.address()!=address){wrongRoute=true;}
        else {incomingUtp.insert(event->endpoint);}
      }
      if(const auto* event=lt::alert_cast<lt::peer_connect_alert>(alert)) {
        if(event->socket_type!=lt::socket_type_t::utp||event->endpoint.address()!=address){wrongRoute=true;}
      }
      if(const auto* event=lt::alert_cast<lt::peer_log_alert>(alert)) {
        // Before attach_to_torrent succeeds, LT has no torrent handle and
        // reports invalid_info_hash through this actual same-endpoint log.
        const std::string_view kind(event->event_type);
        const lt::error_code invalid=lt::errors::invalid_info_hash;
        if((kind=="CONNECTION_FAILED"||kind=="PEER_ERROR")
            &&std::string_view(event->log_message()).find(invalid.message())!=std::string_view::npos) {
          invalidHash.insert(event->endpoint);
        }
        if(kind=="CONNECTION_FAILED"||kind=="PEER_ERROR"){error(std::string(kind)+": "+event->log_message());}
        if(event->direction==lt::peer_log_alert::outgoing_message
            &&std::string_view(event->event_type)=="HANDSHAKE") {
          if(event->endpoint.address()!=address){wrongRoute=true;}
          else if(std::string_view(event->log_message()).starts_with("ih: ")) {
            handshakes.emplace(event->log_message()+4);
          }
        }
      }
      if(const auto* event=lt::alert_cast<lt::peer_error_alert>(alert)) {
        error("peer_error: "+event->error.message());
        if(event->error==lt::errors::invalid_info_hash){invalidHash.insert(event->endpoint);}
      }
      if(const auto* event=lt::alert_cast<lt::peer_disconnected_alert>(alert)) {
        error("peer_disconnected: "+event->error.message());
        if(event->error==lt::errors::invalid_info_hash&&event->socket_type==lt::socket_type_t::utp) {
          invalidHash.insert(event->endpoint);
        }
      }
    } // No borrowed alert, message or peer pointer survives this pop.
  }
  void peers(const lt::torrent_handle& handle,const lt::address& address) {
    std::vector<lt::peer_info> peers;handle.get_peer_info(peers);peerCount=peers.size();
    for(const auto& peer:peers) {
      maximumPeerSent=std::max(maximumPeerSent,peer.total_upload);maximumPeerReceived=std::max(maximumPeerReceived,peer.total_download);
      if(peer.total_upload<=0&&peer.total_download<=0){continue;}
      if(!(peer.flags & lt::peer_info::utp_socket)||peer.ip.address()!=address
          ||peer.local_endpoint.address()!=address){wrongRoute=true;}
      else {payload=true;}
    }
  }
  bool actualUtpHashRefusal() const {
    return std::ranges::any_of(incomingUtp,[&](const auto& peer){return invalidHash.contains(peer);});
  }
};
std::string selectedUtpStatus(const lt::torrent_handle& handle) {
  const auto state=handle.status();std::ostringstream text;
  text<<"phase="<<static_cast<int>(state.state)<<" seeding="<<state.is_seeding
      <<" paused="<<bool(state.flags & lt::torrent_flags::paused)<<" done="<<state.total_wanted_done<<'/'<<state.total_wanted
      <<" received="<<state.total_payload_download<<" sent="<<state.total_payload_upload<<" peers="<<state.num_peers
      <<" error="<<state.errc.message();return text.str();
}
bool selectedUtpPayloadHashes(const std::filesystem::path& file,const lt::torrent_info& info) {
  const auto bytes=selectedFixture::read(file);
  return bytes==std::string(16384,'s')&&info.num_pieces()==1
      &&lt::hasher(bytes.data(),static_cast<int>(bytes.size())).final()==info.hash_for_piece(lt::piece_index_t{0})
      &&lt::hasher256(bytes.data(),static_cast<int>(bytes.size())).final()==info.files().root(lt::file_index_t{0});
}
#endif
}
extern "C" int __real_getifaddrs(ifaddrs**);
extern "C" int __wrap_getifaddrs(ifaddrs** out){
  if(failEnumeration){++failedEnumerations;*out=nullptr;errno=EIO;return -1;}return __real_getifaddrs(out);
}
class NetworkBindingOwnerTests final:public QObject{
  Q_OBJECT
private slots:
  void actualLoopbackRoutes_data(){
    QTest::addColumn<bool>("ipv6");QTest::addColumn<bool>("utp");
    QTest::newRow("tcp-v4")<<false<<false;QTest::newRow("utp-v4")<<false<<true;
    QTest::newRow("tcp-v6")<<true<<false;QTest::newRow("utp-v6")<<true<<true;
  }
  void actualLoopbackRoutes(){
    QFETCH(bool,ipv6);QFETCH(bool,utp);
    test::OwnedEngineFixture fixture(false);const auto address=ipv6?QHostAddress(QHostAddress::LocalHostIPv6):QHostAddress(QHostAddress::LocalHost);
    auto p=localPolicy(ipv6,utp);auto resolved=engine::prepareNetworkBinding(p);QVERIFY2(resolved,"owned loopback family must exist; absence is a failure, never skipped");
    auto pack=engine::makeNetworkSettings(resolved->effective);QVERIFY(pack);
    auto seeder=std::make_unique<lt::session>(*pack);lt::error_code error;
    const auto raw=test::read(fixture.stateFile).toStdString();auto metadata=std::make_shared<lt::torrent_info>(raw.data(),static_cast<int>(raw.size()),error);QVERIFY(!error);
    metadata->clear_trackers();lt::add_torrent_params seed;seed.ti=metadata;seed.save_path=(fixture.root.path()+"/seed").toStdString();
    seed.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);seed.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_pex|lt::torrent_flags::disable_lsd;
    const auto handle=seeder->add_torrent(seed,error);QVERIFY(!error&&handle.is_valid());
    QTRY_VERIFY_WITH_TIMEOUT(seeder->listen_port()!=0&&handle.status().is_seeding,30000);
    QTcpServer server;QVERIFY(server.listen(address,0));unsigned requests=0;bool wrongRoute=false;
    connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()){
      auto* socket=server.nextPendingConnection();auto pending=std::make_shared<QByteArray>();
      wrongRoute=wrongRoute||socket->peerAddress()!=address||socket->localAddress()!=address;
      connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      connect(socket,&QTcpSocket::readyRead,socket,[&,socket,pending]{
        *pending+=socket->readAll();if(pending->size()>16384){wrongRoute=true;socket->abort();return;}
        if(!pending->contains("\r\n\r\n")){return;}
        if(!pending->startsWith("GET /announce?")||!pending->contains("info_hash=")){wrongRoute=true;socket->abort();return;}
        ++requests;const auto peer=compactPeer(ipv6,seeder->listen_port());const QByteArray body="d8:intervali3600e"+QByteArray(ipv6?"6:peers618:":"5:peers6:")+peer+'e';
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();pending->clear();
      });
    }});
    tracker(fixture,QStringLiteral("http://%1:%2/announce").arg(ipv6?QStringLiteral("[::1]"):QStringLiteral("127.0.0.1")).arg(server.serverPort()));
    p.httpTrackers=true;auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto owner=engine::PersistentSession::create({190,191,static_cast<std::uint64_t>(ipv6?utp?4:3:utp?2:1)});QVERIFY2(owner,"source-bound runtime required; no unqualified skip");
    QVERIFY((*owner)->configureNetwork(p));QVERIFY((*owner)->startIdle(*lease));const auto actual=(*owner)->liveSettings();QVERIFY(actual);
    const auto endpoint=ipv6?"::1":"127.0.0.1";QCOMPARE(actual->outgoingInterfaces,std::string(endpoint));QVERIFY(!actual->dht&&!actual->localDiscovery&&!actual->upnp&&!actual->natpmp);
    QVERIFY((*owner)->startManaged(*lease,{*fixture.hash,0,true,false}));
    const auto completed=[&]{static_cast<void>((*owner)->poll());const auto rows=(*owner)->managedDownloads();return rows.size()==1&&rows[0].complete&&rows[0].received>0;};
    QTRY_VERIFY_WITH_TIMEOUT(completed(),30000);QVERIFY(requests>0);QVERIFY(!wrongRoute);
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);QVERIFY(fixture.intact());
  }
#if __has_include(<libtorrent/biglybtc_swarm.hpp>)
  void selectedHybridUtpOwnerRoutes_data(){
    QTest::addColumn<bool>("ipv6");QTest::addColumn<bool>("v2");
    QTest::newRow("selected-v1-utp-v4")<<false<<false;
    QTest::newRow("selected-v2-utp-v4")<<false<<true;
    QTest::newRow("selected-v1-utp-v6")<<true<<false;
    QTest::newRow("selected-v2-utp-v6")<<true<<true;
  }
  void selectedHybridUtpOwnerRoutes(){
    QFETCH(bool,ipv6);QFETCH(bool,v2);
    const auto bridge=engine::detail::managedPayloadCounterBridge();
    QVERIFY2(bridge,"Exact full selected-swarm provider proof required, never skip/fallback");
    QCOMPARE(lt::biglybtc_swarm_mask_abi_v1(),1U);
    selectedFixture::Fixture fixture(v2);auto metadata=selectedUtpMetadata(fixture);
    const auto full=metadata->info_hashes();const auto selected=full.get(v2?lt::protocol_version::V2:lt::protocol_version::V1);
    const auto address=lt::make_address(ipv6?"::1":"127.0.0.1");
    const auto seedPath=fixture.root/"seed";QVERIFY(std::filesystem::create_directory(seedPath));
    selectedFixture::write(seedPath/"selected.bin",std::string(16384,'s'));
    auto settings=selectedUtpSettings(ipv6);settings.set_int(lt::settings_pack::upload_rate_limit,4096);
    auto seeder=std::make_unique<lt::session>(settings);
    auto parameters=selectedUtpParameters(metadata,seedPath);
    QVERIFY(engine::detail::selectManagedSwarm(parameters,*fixture.identity,bridge));
    lt::error_code error;const auto seed=seeder->add_torrent(std::move(parameters),error);QVERIFY(!error&&seed.is_valid());
    QTRY_VERIFY_WITH_TIMEOUT(seeder->listen_port()!=0&&seed.status().is_seeding,30000);
    QVERIFY(seeder->find_torrent(selected).is_valid());
    QVERIFY(!seeder->find_torrent(full.get(v2?lt::protocol_version::V1:lt::protocol_version::V2)).is_valid());
    QTcpServer server;QVERIFY(server.listen(ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,0));
    bool wrongTracker=false;unsigned requests=0;std::set<std::string> trackerHashes;
    const QByteArray expectedHash(selected.data(),selected.size());
    connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()){
      auto* socket=server.nextPendingConnection();auto pending=std::make_shared<QByteArray>();
      connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      connect(socket,&QTcpSocket::readyRead,socket,[&,socket,pending]{
        *pending+=socket->readAll();if(pending->size()>16384){wrongTracker=true;socket->abort();return;}
        if(!pending->contains("\r\n\r\n")){return;}
        const auto at=pending->indexOf("info_hash=");
        const auto end=at<0?-1:pending->indexOf('&',at);
        const auto space=at<0?-1:pending->indexOf(' ',at);
        const auto stop=end<0?space:space<0?end:std::min(end,space);
        if(at>=0&&stop>=at+10&&trackerHashes.size()<8){trackerHashes.insert(QByteArray::fromPercentEncoding(pending->mid(at+10,stop-at-10)).toHex().toStdString());}
        if(!pending->startsWith("GET /announce?")||at<0||stop<at+10
            ||QByteArray::fromPercentEncoding(pending->mid(at+10,stop-at-10))!=expectedHash){
          wrongTracker=true;socket->abort();return;
        }
        const auto qtAddress=ipv6?QHostAddress(QHostAddress::LocalHostIPv6):QHostAddress(QHostAddress::LocalHost);
        if(socket->peerAddress()!=qtAddress||socket->localAddress()!=qtAddress){wrongTracker=true;socket->abort();return;}
        ++requests;const auto peer=compactPeer(ipv6,seeder->listen_port());
        const QByteArray body="d8:intervali3600e"+QByteArray(ipv6?"6:peers618:":"5:peers6:")+peer+'e';
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
        socket->disconnectFromHost();pending->clear();
      });
    }});
    const auto url=QStringLiteral("http://%1:%2/announce").arg(ipv6?QStringLiteral("[::1]"):QStringLiteral("127.0.0.1")).arg(server.serverPort()).toUtf8();
    auto document=core::Document::parse(selectedFixture::read(fixture.state));QVERIFY(document);
    QVERIFY(document->replaceValue("announce",std::to_string(url.size())+":"+url.toStdString()));
    // Replace only this generated test state, before owner admission.
    std::filesystem::remove(fixture.state);selectedFixture::write(fixture.state,document->serialize());
    auto made=engine::PersistentSession::create({210,211,static_cast<std::uint64_t>(ipv6?(v2?4:3):(v2?2:1))});
    QVERIFY(made);auto owner=std::move(*made);auto policy=localPolicy(ipv6,true);policy.httpTrackers=true;
    QVERIFY(owner->configureNetwork(policy));QVERIFY(owner->startIdle(*fixture.lease));
    QVERIFY(!owner->tcpListenPort()); // TCP cannot carry this transfer.
    QVERIFY(owner->startManaged(*fixture.lease,{.hash=*fixture.key,.forceStart=true}));
    SelectedUtpEvidence evidence;
    const auto complete=[&]{
      static_cast<void>(owner->poll());evidence.pop(*seeder,address);evidence.peers(seed,address);
      const auto rows=owner->managedDownloads();
      return rows.size()==1&&rows[0].complete&&rows[0].received>0&&evidence.payload
          &&evidence.handshakes.contains(selectedUtpHashText(selected));
    };
    const auto diagnostic=[&]{
      std::ostringstream text;text<<"selected="<<selectedUtpHashText(selected)<<" requests="<<requests<<" wrongTracker="<<wrongTracker;
      text<<" trackerHashes=[";for(const auto& hash:trackerHashes){text<<hash<<',';}text<<']';
      const auto rows=owner->managedDownloads();text<<" managedRows="<<rows.size();
      for(const auto& row:rows){text<<" managed{complete="<<row.complete<<" checked="<<row.checked<<" selectedComplete="<<row.selectedComplete
          <<" received="<<row.received<<" sent="<<row.sent<<" done="<<row.completed<<'/'<<row.bytes<<" phase="<<static_cast<int>(row.phase)
          <<" retained="<<row.retained<<" failure="<<(row.failure?row.failure->message:"none")<<'}';}
      text<<" seed{"<<selectedUtpStatus(seed)<<"} evidence{"<<evidence.diagnostic()<<'}';return QByteArray::fromStdString(text.str());
    };
    QTRY_VERIFY2_WITH_TIMEOUT(complete(),diagnostic().constData(),30000);
    QVERIFY(requests>0&&!wrongTracker);QVERIFY(!evidence.dropped&&!evidence.wrongRoute);
    QVERIFY(seed.info_hashes()==full);QCOMPARE(std::string(seed.torrent_file()->info_section().data(),seed.torrent_file()->info_section().size()),fixture.rawInfo);
    const auto closed=owner->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
    QVERIFY(selectedUtpPayloadHashes(fixture.destination/"selected.bin",*metadata));
    const auto saved=core::Document::parse(selectedFixture::read(fixture.state));QVERIFY(saved);
    const auto raw=storage::originalTorrentInfoSubject(*saved);QVERIFY(raw);QCOMPARE(std::string(*raw),fixture.rawInfo);
    qInfo("Actual selected %s uTP IPv%d ih=%s: peer payload, source route, SHA1+Merkle and joined native close",v2?"V2":"V1",ipv6?6:4,selectedUtpHashText(selected).c_str());
  }
  void bothHybridUtpPayloads_data(){
    QTest::addColumn<bool>("ipv6");QTest::newRow("both-utp-v4")<<false;QTest::newRow("both-utp-v6")<<true;
  }
  void bothHybridUtpPayloads(){
    QFETCH(bool,ipv6);const auto bridge=engine::detail::managedPayloadCounterBridge();QVERIFY(bridge);
    // Both is upstream/default provider behavior, not an original saved-profile
    // selection. Exercise its actual v1 AND v2 paths in separate owned pairs.
    for(const bool v2:{false,true}){
      selectedFixture::Fixture fixture(v2);auto metadata=selectedUtpMetadata(fixture);const auto full=metadata->info_hashes();
      const auto selected=full.get(v2?lt::protocol_version::V2:lt::protocol_version::V1);
      const auto address=lt::make_address(ipv6?"::1":"127.0.0.1");
      const auto seedPath=fixture.root/"seed";QVERIFY(std::filesystem::create_directory(seedPath));
      selectedFixture::write(seedPath/"selected.bin",std::string(16384,'s'));
      auto settings=selectedUtpSettings(ipv6);settings.set_int(lt::settings_pack::upload_rate_limit,4096);
      auto seeder=std::make_unique<lt::session>(settings);auto receiver=std::make_unique<lt::session>(selectedUtpSettings(ipv6));
      auto sp=selectedUtpParameters(metadata,seedPath);QVERIFY(engine::detail::selectManagedSwarm(sp,*fixture.identity,bridge));
      lt::error_code error;const auto seed=seeder->add_torrent(std::move(sp),error);QVERIFY(!error&&seed.is_valid());
      const auto receiving=receiver->add_torrent(selectedUtpParameters(metadata,fixture.destination),error);QVERIFY(!error&&receiving.is_valid());
      QTRY_VERIFY_WITH_TIMEOUT(seed.status().is_seeding&&seeder->listen_port()!=0
          &&receiving.status().state!=lt::torrent_status::checking_files
          &&receiving.status().state!=lt::torrent_status::checking_resume_data,30000);
      QVERIFY(receiver->find_torrent(full.v1).is_valid()&&receiver->find_torrent(full.v2).is_valid());
      auto flags=lt::pex_utp;if(v2){flags|=lt::pex_lt_v2;}
      receiving.connect_peer({address,seeder->listen_port()},lt::peer_source_flags_t{},flags);
      SelectedUtpEvidence sent,received;
      const auto complete=[&]{sent.pop(*seeder,address);received.pop(*receiver,address);sent.peers(seed,address);received.peers(receiving,address);
        return receiving.status().is_seeding&&sent.payload&&received.payload
            &&sent.handshakes.contains(selectedUtpHashText(selected))&&received.handshakes.contains(selectedUtpHashText(selected));};
      const auto diagnostic=[&]{std::ostringstream text;text<<"selected="<<selectedUtpHashText(selected)
          <<" seed{"<<selectedUtpStatus(seed)<<"} receiver{"<<selectedUtpStatus(receiving)
          <<"} sent{"<<sent.diagnostic()<<"} received{"<<received.diagnostic()<<'}';return QByteArray::fromStdString(text.str());};
      QTRY_VERIFY2_WITH_TIMEOUT(complete(),diagnostic().constData(),30000);
      QVERIFY(!sent.dropped&&!received.dropped&&!sent.wrongRoute&&!received.wrongRoute);
      QVERIFY(receiving.info_hashes()==full&&seed.info_hashes()==full);
      receiver.reset();seeder.reset(); // Join both owners before reading their files/removing the fixture.
      QVERIFY(selectedUtpPayloadHashes(fixture.destination/"selected.bin",*metadata));
    }
    qInfo("Actual default Both uTP IPv%d: separate v1/v2 handshake+payload+route+SHA1/Merkle observations",ipv6?6:4);
  }
  void oppositeSelectedSwarmUtpIsActuallyRejected_data(){selectedHybridUtpOwnerRoutes_data();}
  void oppositeSelectedSwarmUtpIsActuallyRejected(){
    QFETCH(bool,ipv6);QFETCH(bool,v2);const auto bridge=engine::detail::managedPayloadCounterBridge();QVERIFY(bridge);
    selectedFixture::Fixture allowed(v2),forbidden(!v2);
    auto info=selectedUtpMetadata(allowed);auto other=selectedUtpMetadata(forbidden);
    QVERIFY(info->info_hashes()==other->info_hashes()); // Only original selected identity differs.
    const auto address=lt::make_address(ipv6?"::1":"127.0.0.1");
    const auto seedPath=forbidden.root/"seed";QVERIFY(std::filesystem::create_directory(seedPath));
    selectedFixture::write(seedPath/"selected.bin",std::string(16384,'s'));
    auto receiver=std::make_unique<lt::session>(selectedUtpSettings(ipv6));
    auto sender=std::make_unique<lt::session>(selectedUtpSettings(ipv6));
    auto rp=selectedUtpParameters(info,allowed.destination),sp=selectedUtpParameters(other,seedPath);
    QVERIFY(engine::detail::selectManagedSwarm(rp,*allowed.identity,bridge));
    QVERIFY(engine::detail::selectManagedSwarm(sp,*forbidden.identity,bridge));
    lt::error_code error;const auto receive=receiver->add_torrent(std::move(rp),error);QVERIFY(!error&&receive.is_valid());
    const auto send=sender->add_torrent(std::move(sp),error);QVERIFY(!error&&send.is_valid());
    QTRY_VERIFY_WITH_TIMEOUT(receiver->listen_port()!=0&&send.status().is_seeding
        &&receive.status().state!=lt::torrent_status::checking_files
        &&receive.status().state!=lt::torrent_status::checking_resume_data,30000);
    const auto hash=info->info_hashes().get(v2?lt::protocol_version::V1:lt::protocol_version::V2);
    QVERIFY(!receiver->find_torrent(hash).is_valid());
    auto flags=lt::pex_utp;if(!v2){flags|=lt::pex_lt_v2;}
    send.connect_peer({address,receiver->listen_port()},lt::peer_source_flags_t{},flags);
    SelectedUtpEvidence accepted,rejected;
    const auto refused=[&]{accepted.pop(*receiver,address);rejected.pop(*sender,address);
      return accepted.actualUtpHashRefusal()&&rejected.handshakes.contains(selectedUtpHashText(hash));};
    // Requires a real accepted uTP socket and invalid_info_hash for THAT endpoint;
    // neither a timeout nor zero bytes alone can pass this rejection fixture.
    const auto diagnostic=[&]{std::ostringstream text;text<<"forbidden="<<selectedUtpHashText(hash)
        <<" receiver{"<<selectedUtpStatus(receive)<<"} sender{"<<selectedUtpStatus(send)
        <<"} accepted{"<<accepted.diagnostic()<<"} rejected{"<<rejected.diagnostic()<<'}';return QByteArray::fromStdString(text.str());};
    QTRY_VERIFY2_WITH_TIMEOUT(refused(),diagnostic().constData(),15000);
    QVERIFY(!accepted.dropped&&!rejected.dropped&&!accepted.wrongRoute&&!rejected.wrongRoute);
    QCOMPARE(receive.status().total_payload_download,std::int64_t{0});
    QCOMPARE(send.status().total_payload_upload,std::int64_t{0});
    receiver.reset();sender.reset();
    if(std::filesystem::exists(allowed.destination/"selected.bin")){
      const auto bytes=selectedFixture::read(allowed.destination/"selected.bin");QVERIFY(bytes!=std::string(16384,'s'));
    }
    qInfo("Actual forbidden opposite swarm rejected by invalid_info_hash on uTP IPv%d; no payload",ipv6?6:4);
  }
#endif
  void missingSelectedInterfacePreventsStartup(){
    test::OwnedEngineFixture fixture(false);auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto owner=engine::PersistentSession::create({195,196,197});QVERIFY(owner);auto p=localPolicy(false,false);p.bindAddresses={"lo[4095]"};
    QVERIFY((*owner)->configureNetwork(p));QVERIFY(!(*owner)->startIdle(*lease));QVERIFY(!(*owner)->tcpListenPort());QVERIFY(!(*owner)->managedAvailable());
    const auto result=(*owner)->close(std::chrono::steady_clock::now()+5s);QVERIFY(result.sessionJoined);QVERIFY(!result.retirement.peerCounters);QVERIFY(fixture.intact(false));
  }
  void failedInterfaceObservationJoinsWithoutInventingFinalSample(){
    test::OwnedEngineFixture fixture(false);auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto owner=engine::PersistentSession::create({201,202,203});QVERIFY(owner);QVERIFY((*owner)->configureNetwork(localPolicy(false,false)));QVERIFY((*owner)->startIdle(*lease));
    const auto sampled=[&]{static_cast<void>((*owner)->poll());const auto value=(*owner)->latest();return value&&value->observed.has_value();};QTRY_VERIFY_WITH_TIMEOUT(sampled(),10000);
    const auto before=(*owner)->latest();QVERIFY(before&&before->observed);std::expected<void,engine::AccountingError> failed;
    {FailEnumeration fault;failed=(*owner)->poll();}
    QVERIFY(!failed);QCOMPARE(failedEnumerations,1U);const auto after=(*owner)->latest();QVERIFY(after&&after->observed);QCOMPARE(after->state,engine::AccountingState::Uncertified);
    QCOMPARE(*after->observed,*before->observed);QVERIFY(after->sequence>before->sequence);QVERIFY(!(*owner)->managedAvailable());QVERIFY(!(*owner)->tcpListenPort());
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined);QVERIFY(!closed.retirement.peerCounters);QVERIFY(fixture.intact(false));
  }
};
QTEST_GUILESS_MAIN(NetworkBindingOwnerTests)
#include "network_binding_owner_tests.moc"
