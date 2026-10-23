#include "tracker_statistics_sampler.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>
#include <vector>

namespace {namespace lt=libtorrent;using namespace biglybtc;
lt::settings_pack settings(){lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
  for(auto key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_outgoing_utp,lt::settings_pack::enable_incoming_utp}){p.set_bool(key,false);}
  // Test-only explicit loopback HTTP endpoint; production policy is untouched.
  p.set_bool(lt::settings_pack::ssrf_mitigation,false);p.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::tracker|lt::alert_category::error|lt::alert_category::status));return p;}
}
class TrackerStatisticsSamplerTests final:public QObject {
  Q_OBJECT
private slots:
  void disabledPolicyDoesNotNeedAnyHandleOrSession(){
    const auto off=engine::sampleTrackerStatistics({},core::TrackerPolicy::DisabledByIsolation);
    QCOMPARE(off.state,core::TrackerReadState::DisabledByIsolation);QVERIFY(off.rows.empty());QVERIFY(core::validTrackerStatistics(off));
    QCOMPARE(engine::sampleTrackerStatistics({},core::TrackerPolicy::Permitted).state,core::TrackerReadState::Unavailable);
  }
  void actualLoopbackHttpResponseReachesPerEndpointSnapshot(){
    QTemporaryDir area(QStringLiteral("/tmp/biglybtc-tracker-provider-XXXXXX"));QVERIFY(area.isValid());QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost,0));
    int announces=0;bool unexpectedRequest=false;
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()){
      auto* socket=server.nextPendingConnection();auto pending=std::make_shared<QByteArray>();
      QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,pending]{*pending+=socket->readAll();if(pending->size()>8192){unexpectedRequest=true;socket->abort();return;}
        if(!pending->contains("\r\n\r\n")){return;}if(!pending->startsWith("GET /announce?")||!pending->contains("info_hash=")){unexpectedRequest=true;socket->abort();return;}
        ++announces;const QByteArray body("d8:completei7e10:downloadedi19e10:incompletei3e8:intervali3600e5:peers0:e");
        const QByteArray reply="HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;
        socket->write(reply);socket->disconnectFromHost();pending->clear();
      });
    }});
    const QByteArray payload(16384,'a');QFile file(area.path()+QStringLiteral("/sample.bin"));QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(file.write(payload),payload.size());QVERIFY(file.flush());file.close();
    lt::file_storage files;files.add_file("sample.bin",payload.size());lt::create_torrent create(files,16384,lt::create_torrent::v1_only);
    create.set_hash(lt::piece_index_t{0},lt::hasher(payload.constData(),static_cast<int>(payload.size())).final());
    const auto url="http://127.0.0.1:"+std::to_string(server.serverPort())+"/announce";create.add_tracker(url,3);
    std::vector<char> encoded;lt::bencode(std::back_inserter(encoded),create.generate());lt::error_code error;
    auto info=std::make_shared<lt::torrent_info>(lt::span<const char>(encoded.data(),encoded.size()),error,lt::from_span);QVERIFY(!error);
    lt::session session(settings());lt::add_torrent_params params;params.ti=info;params.save_path=area.path().toStdString();
    params.flags&=~lt::torrent_flags::paused;params.flags&=~lt::torrent_flags::auto_managed;params.flags|=lt::torrent_flags::seed_mode;
    const auto handle=session.add_torrent(params,error);QVERIFY(!error);QVERIFY(handle.is_valid());
    bool fatal=false;core::TrackerStatistics observed;std::vector<lt::alert*> alerts;
    const auto received=[&]{session.pop_alerts(&alerts); // Sole consumer; no pointer survives the next pop.
      for(const auto* a:alerts){if(lt::alert_cast<lt::torrent_error_alert>(a)||lt::alert_cast<lt::file_error_alert>(a)||lt::alert_cast<lt::session_error_alert>(a)){fatal=true;}}
      observed=engine::sampleTrackerStatistics(handle,core::TrackerPolicy::Permitted);
      for(const auto& row:observed.rows){if(row.verified&&row.protocol==1&&row.seeders==7&&row.leechers==3&&row.downloaded==19){return true;}}return false;};
    QTRY_VERIFY_WITH_TIMEOUT(received(),15000);QVERIFY(!fatal);QVERIFY(!unexpectedRequest);QVERIFY(announces>=1);QVERIFY(core::validTrackerStatistics(observed));
    bool exact=false;for(const auto& row:observed.rows){if(row.verified&&row.protocol==1){QVERIFY(row.enabled.has_value());QVERIFY(row.updating.has_value());QCOMPARE(row.failures,std::optional<int>{0});
      QVERIFY(row.nextAnnounceSeconds.has_value());QVERIFY(!row.localEndpoint.empty());QCOMPARE(row.displayUrl,"http://127.0.0.1:"+std::to_string(server.serverPort())+"/[path hidden]");exact=true;}}
    QVERIFY(exact);
    const auto disabled=engine::sampleTrackerStatistics(handle,core::TrackerPolicy::DisabledByIsolation);QVERIFY(disabled.rows.empty());QCOMPARE(disabled.state,core::TrackerReadState::DisabledByIsolation);
    session.remove_torrent(handle);server.close();QFile untouched(area.path()+QStringLiteral("/sample.bin"));QVERIFY(untouched.open(QIODevice::ReadOnly));QCOMPARE(untouched.readAll(),payload);
  }
};
QTEST_GUILESS_MAIN(TrackerStatisticsSamplerTests)
#include "tracker_statistics_sampler_tests.moc"
