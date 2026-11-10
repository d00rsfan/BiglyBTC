// SPDX-License-Identifier: GPL-2.0-or-later
#include "provider.hpp"
#include "room.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <QTest>
#include <QUdpSocket>
#include <QScopeGuard>
#include <stdexcept>
using namespace biglybtc;using namespace std::chrono_literals;
namespace udp=services::azureus_udp;namespace shared=services::shared_udp;namespace lt=libtorrent;
namespace {
void need(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
struct Actor {
  lt::session session;std::unique_ptr<shared::Provider> provider;
  udp::OwnedPingTransport transport;QObject receiver;bool permit=true;std::uint64_t revision=1;
  unsigned delivered=0;
  Actor(const QHostAddress& address,bool v6):session([&]{
    QUdpSocket reservation;need(reservation.bind(address,0),"Owned shared UDP port");const auto port=reservation.localPort();reservation.close();
    lt::session_params p;p.settings.set_str(lt::settings_pack::listen_interfaces,
      (v6?"["+address.toString().toStdString()+"]":address.toString().toStdString())+":"+std::to_string(port));
    p.settings.set_bool(lt::settings_pack::enable_dht,false);p.settings.set_bool(lt::settings_pack::enable_lsd,false);p.settings.set_bool(lt::settings_pack::enable_upnp,false);p.settings.set_bool(lt::settings_pack::enable_natpmp,false);return p;
  }()),transport([&]{udp::Config c;c.network=v6?3:0;c.servePings=true;return c;}()){
    const auto end=std::chrono::steady_clock::now()+3s;
    // Session settings give the actual selected listener without inventing a
    // second port. The fixture reserves that port only until SDK construction.
    while(!provider&&std::chrono::steady_clock::now()<end){
      const auto port=session.listen_port();if(port>0){auto attached=shared::Provider::attach(session,static_cast<quint16>(port));if(attached){provider=std::move(*attached);break;}}
      QTest::qWait(5);
    }
    need(bool(provider),"Qualified shared provider attachment");
    const auto local=provider->bindings();auto found=std::ranges::find_if(local,[&](const auto& v){return v.address==address;});need(found!=local.end(),"Exact actual SDK listener");
    auto channel=provider->channel(*found,*found,[this]{return permit;});need(bool(channel),"Shared channel");need(bool(transport.startShared(std::move(*channel))),"Shared actor start");
    provider->receive([this](auto local,auto peer,std::string data){return local==transport.endpoint()&&transport.receiveShared(std::move(peer),std::move(data));});
    services::azureus_nodes::ResolvedEndpoint endpoint;endpoint.address.length=v6?16:4;endpoint.address.port=transport.endpoint().port;
    if(v6){const auto ip=address.toIPv6Address();std::copy_n(ip.c,16,endpoint.address.bytes.begin());}else{const auto ip=address.toIPv4Address();for(unsigned i=0;i<4;++i){endpoint.address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}
    services::azureus_nodes::NodeCache cache;const auto node=cache.lookup(endpoint,55);need(bool(node),"Actor contact");auto router=services::azureus_router::RouterOwner::create(node->id);auto database=services::azureus_db_runtime::Owner::create();need(bool(router)&&bool(database),"Actor router/database");
    need(bool((*database)->bindLocal(services::azureus_nodes::Contact{.protocol=55,.address=endpoint.address,.id=node->id})),"Actual local contact binding");
    need(bool(transport.serveDatabase(std::move(*router),std::move(*database),[]{return true;},[](auto completion){completion(true);return true;})),"Actual actor service");
  }
  bool room(std::string key,std::function<std::string(std::string)> answer){return bool(transport.registerTransferHandler(std::move(key),true,&receiver,[this,answer=std::move(answer)](auto request){++delivered;need(bool(transport.answerTransfer(request->receipt,answer(request->request))),"Actor answer");},[this]()->std::optional<std::uint64_t>{return revision;}));}
};
}
class SharedUdpTests final:public QObject {
 Q_OBJECT
private slots:
 void actualSdkActorRoundtrip_data(){QTest::addColumn<bool>("v6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void actualSdkActorRoundtrip(){
  QFETCH(bool,v6);Actor a(QHostAddress(v6?"::1":"127.121.11.1"),v6),b(QHostAddress(v6?"::1":"127.121.11.2"),v6);
  QVERIFY(a.transport.allowPeer(b.transport.endpoint()));QVERIFY(b.transport.allowPeer(a.transport.endpoint()));
  const std::string key(20,'G');QVERIFY(a.room(key,[](std::string s){return s;}));QVERIFY(b.room(key,[](std::string s){return "reply:"+s;}));
  std::optional<udp::TransferOutcome> answer;const std::string payload(6000,'q');
  QVERIFY(a.transport.callTransfer(b.transport.endpoint(),55,key,payload,&a.receiver,[&](auto out){answer=std::move(out);},std::chrono::steady_clock::now()+5s));
  QTRY_VERIFY_WITH_TIMEOUT(answer.has_value(),5000);QVERIFY(*answer);QCOMPARE(**answer,"reply:"+payload);QCOMPARE(b.delivered,1u);
  std::optional<udp::Outcome> ping;QVERIFY(b.transport.ping(a.transport.endpoint(),55,&b.receiver,[&](auto out){ping=std::move(out);},1s));QTRY_VERIFY_WITH_TIMEOUT(ping.has_value(),1500);QVERIFY(*ping);
  // A stale source permission does not send through a still-live SDK binding.
  a.permit=false;QVERIFY(!a.transport.ping(b.transport.endpoint(),55,&a.receiver,[](auto){},1s));
  auto denied=a.provider->channel(a.transport.endpoint(),a.transport.endpoint(),[]{return false;});QVERIFY(denied);
  QVERIFY(!denied->send("invalid",b.transport.endpoint(),std::chrono::steady_clock::now()+1s));
  a.provider->close();QVERIFY(!denied->current());QVERIFY(!a.transport.receiveShared(b.transport.endpoint(),"stale"));
 }
 void sharedIngressIsBoundedAndDoesNotGrantPeerAdmission(){
  Actor a(QHostAddress("127.121.12.1"),false);const udp::Peer unknown{QHostAddress("127.121.12.2"),54321};
  unsigned accepted=0;for(unsigned i=0;i<1000;++i){accepted+=a.transport.receiveShared(unknown,std::string(20,'x'));}QCOMPARE(accepted,64u);
  QTRY_COMPARE_WITH_TIMEOUT(a.transport.statistics().received,std::uint64_t{64},1000);
  QCOMPARE(a.transport.statistics().unowned,std::uint64_t{64});QVERIFY(!a.transport.isRoutingPeer(unknown));
  a.transport.close();QVERIFY(!a.transport.receiveShared(unknown,"late"));
 }
 void sdkCallbacksDeliverOnTheQtOwnerAndOldAttachmentCanBeReplaced(){
  Actor a(QHostAddress("127.121.13.1"),false);auto old=a.provider->channel(a.transport.endpoint(),a.transport.endpoint(),[]{return true;});QVERIFY(old);
  a.provider->close();QVERIFY(!old->current());
  const bool existed=qEnvironmentVariableIsSet("LD_PRELOAD");const auto saved=qgetenv("LD_PRELOAD");
  {const auto restored=qScopeGuard([&]{if(existed){qputenv("LD_PRELOAD",saved);}else{qunsetenv("LD_PRELOAD");}});
    qputenv("LD_PRELOAD","generated-declared-preload-policy");
    QVERIFY(!shared::Provider::attach(a.session,a.transport.endpoint().port));
  }
  auto replacement=shared::Provider::attach(a.session,a.transport.endpoint().port);QVERIFY(replacement);QVERIFY((*replacement)->refresh());
  QUdpSocket sender;QVERIFY(sender.bind(QHostAddress("127.121.13.2"),0));bool delivered=false;
  (*replacement)->receive([&](auto,auto,std::string){delivered=QThread::currentThread()==thread();return true;});
  QByteArray data(32,'x');data[0]=char(0x80);data[8]=0;data[9]=0;data[10]=4;data[11]=0;
  QCOMPARE(sender.writeDatagram(data,a.transport.endpoint().address,a.transport.endpoint().port),qint64(data.size()));QTRY_VERIFY_WITH_TIMEOUT(delivered,1000);
 }
};
QTEST_GUILESS_MAIN(SharedUdpTests)
#include "shared_udp_tests.moc"
