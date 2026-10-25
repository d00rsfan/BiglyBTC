#include "imported_scheduler_fixture.hpp"
#include "library_view.hpp"
#include "persistent_session.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <filesystem>
#include <functional>
#include <iostream>

using namespace biglybtc;
namespace sh=test::scheduler_host;
namespace {
QJsonObject receipt(const QString& path){
  const auto info=QFileInfo(path);sh::require(info.isAbsolute()&&!info.isSymLink()&&info.isFile(),"Owned receipt file");
  auto value=QJsonDocument::fromJson(sh::read(path));sh::require(value.isObject(),"Generated receipt object");
  const auto object=value.object();const auto root=object["root"].toString();
  sh::require(root.startsWith("/tmp/biglybtc-engine-statistics-")&&QFileInfo(root).canonicalFilePath()==root,"Generated canonical fixture root");
  sh::require(object["profile"].toString()==root+"/profile","Exact generated profile");return object;
}
void wait(const std::function<bool()>& condition,int milliseconds,const char* reason){
  QElapsedTimer clock;clock.start();
  while(!condition()&&clock.elapsed()<milliseconds){QCoreApplication::processEvents();QTest::qWait(25);}
  sh::require(condition(),reason);
}
void run(const QStringList& args){
  sh::require(args.size()>=3,"prepare RECEIPT single|multi ordinary|force, or execute RECEIPT");
  const auto mode=args[1],path=args[2];
  if(mode=="prepare"){
    sh::require(args.size()==5&&(args[3]=="single"||args[3]=="multi")&&(args[4]=="ordinary"||args[4]=="force"),"Finite fixture choices");
    sh::require(!QFile::exists(path)&&QFileInfo(path).absolutePath().startsWith("/tmp/biglybtc-"),"Fresh owned receipt path");
    const bool multi=args[3]=="multi",force=args[4]=="force";test::OwnedEngineFixture f(multi);
    test::imported_scheduler::prepare(f,false,force);
    {auto held=storage::ProfileLease::acquire(f.native());sh::require(bool(held),"Prepare held lease");
      sh::require(bool(storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::Resume)),"Unpaused starting state for actual Java pause");}
    sh::write(sh::schedulerFile(f.profile,"PausedTorrents.conf"),"opaque survivor\r\n");
    f.root.setAutoRemove(false);QJsonObject value;value["root"]=f.root.path();value["profile"]=f.profile;
    value["hash"]=QString::fromStdString(f.hash->hex());value["forced"]=force;value["multifile"]=multi;value["joined"]=false;
    sh::write(path,QJsonDocument(value).toJson());std::cout<<"PREPARED\n";return;
  }
  sh::require(mode=="execute"&&args.size()==3,"Explicit execution mode");auto value=receipt(path);
  const auto profile=value["profile"].toString();
  const auto config=sh::document(sh::read(profile+"/biglybt.config"));
  const auto policy=engine::readNetworkPolicy(&config,{.mode=engine::NetworkMode::Normal});
  sh::require(policy&&policy->proxy.kind==engine::NetworkProxyKind::None&&policy->bindAddresses==std::vector<std::string>{"127.0.0.1"}&&!policy->outgoingUtp&&!policy->dht
      &&!policy->pex&&!policy->lsd&&!policy->upnp&&!policy->natpmp&&!policy->httpTrackers&&!policy->udpTrackers,
      "Numeric loopback profile with all discovery/tracker providers disabled");
  const auto raw=QByteArray::fromHex(value["hash"].toString().toLatin1());
  const auto decoded=core::TorrentHash::parse(std::string_view(raw.constData(),static_cast<std::size_t>(raw.size())));
  sh::require(bool(decoded),"Exact generated hash");sh::ApprovedModule module;
  app::SessionBridge bridge(nullptr,{},{},module.factory());QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed),native(&bridge,&app::SessionBridge::nativeSessionReady);
  sh::require(bridge.openNormal(profile.toStdString()),"Open generated imported profile");
  wait([&]{return !bridge.busy()&&!rows.empty()
      &&sh::read(sh::schedulerFile(profile,"PausedTorrents.conf"))==QByteArray("opaque survivor\r\n");},45000,"Imported real command deadline");
  wait([&]{for(const auto& row:native){const auto packet=qvariant_cast<ui::NativeSessionPresentation>(row[0]);
    if(packet&&packet->state==ui::NativeSessionState::Active){return true;}}return false;},15000,"Actual native admission observation");
  sh::require(bridge.closeSession(),"Joined close accepted");wait([&]{return !closed.empty();},15000,"Joined close deadline");
  sh::require(closed.first()[1].toBool()&&closed.first()[2].toBool(),"Actual storage release and owner join");
  {auto held=storage::ProfileLease::acquire(std::filesystem::path(profile.toStdString()));sh::require(bool(held),"Sole lease reacquired");
    const auto library=storage::readLibraryView(*held);sh::require(library&&library->items.size()==1&&library->pauses.empty(),"Imported pause consumed");
    sh::require(library->items[0].saved.forceStart==value["forced"].toBool(),"Saved original force restoration");}
  value["joined"]=true;sh::write(path,QJsonDocument(value).toJson());std::cout<<"IMPORTED_HOST_JOINED\n";
}
}
int main(int argc,char** argv){
  QApplication app(argc,argv);QApplication::setQuitOnLastWindowClosed(false);
  try{run(app.arguments());return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
