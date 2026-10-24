#pragma once
#include "session_bridge.hpp"
#include "scheduler_service.hpp"
#include "biglybtc/bencode.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <filesystem>
#include <stdexcept>

namespace biglybtc::test::scheduler_host {
inline void require(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
inline QByteArray read(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Read owned scheduler file");return f.readAll();}
inline void write(const QString& path,const QByteArray& bytes){require(QDir().mkpath(QFileInfo(path).absolutePath()),"Create owned scheduler parent");QFile f(path);require(f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(bytes)==bytes.size()&&f.flush(),"Write owned scheduler file");}
inline core::Document document(const QByteArray& bytes){auto p=core::Document::parse(bytes.toStdString(),storage::compatibleConfigLimits());require(p.has_value(),"Parse generated scheduler document");return std::move(*p);}
inline const QByteArray Xml=R"(<?xml version="1.0"?><SpeedScheduler opaque="keep"><!--untouched--><defaultRates upload="7" download="9" enforce="1"/><schedules><schedule enabled="true"><startTime hour="0" minute="0"/><endTime hour="23" minute="59"/><rates upload="17" download="19" pauseDownloads="true" pauseSeeds="true"/><days><day index="0" selected="true"/><day index="1" selected="true"/><day index="2" selected="true"/><day index="3" selected="true"/><day index="4" selected="true"/><day index="5" selected="true"/><day index="6" selected="true"/></days><future value="preserved"/></schedule></schedules></SpeedScheduler>)";
inline QString schedulerFile(const QString& profile,const char* leaf="SavedSchedules.xml"){return profile+QStringLiteral("/plugins/SpeedScheduler/")+QString::fromLatin1(leaf);}
inline void original(const QString& profile){write(schedulerFile(profile,"plugin.properties"),"plugin.id=SpeedScheduler\nplugin.version=1.8\n");write(schedulerFile(profile),Xml);}
class ApprovedModule final {
  QTemporaryDir root_{QStringLiteral("/tmp/biglybtc-scheduler-host-module-XXXXXX")};
  std::filesystem::path path_;
  storage::SchedulerModuleApproval approval_;
public:
  ApprovedModule(){
    const auto source=qEnvironmentVariable("BIGLYBTC_SCHEDULER_TEST_MODULE");
    const QFileInfo file(source);require(root_.isValid()&&file.isAbsolute()&&file.isFile()&&!file.isSymLink(),"Set BIGLYBTC_SCHEDULER_TEST_MODULE to actual built approved module");
    const auto target=root_.path()+QStringLiteral("/scheduler.so");require(QFile::copy(source,target),"Copy actual built scheduler into owned fixture");
    require(QFile::setPermissions(target,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner),"Set private module fixture permissions");
    const auto bytes=read(target);path_=target.toStdString();approval_={QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString(),static_cast<std::uint64_t>(bytes.size()),"0.1.0"};
  }
  app::SessionBridge::SchedulerFactory factory()const{
    return [path=path_,approval=approval_](const storage::ProfileLease& lease){auto service=storage::loadApprovedSchedulerService(lease,path,approval);if(!service){throw std::runtime_error(service.error().message);}return std::move(*service);};
  }
};
inline ui::SchedulerPresentation last(const QSignalSpy& spy){return spy.empty()?ui::SchedulerPresentation{}:qvariant_cast<ui::SchedulerPresentation>(spy.last()[0]);}
inline ui::SchedulerSavePresentation saved(const QSignalSpy& spy){return spy.empty()?ui::SchedulerSavePresentation{}:qvariant_cast<ui::SchedulerSavePresentation>(spy.last()[0]);}
inline ui::SchedulerSubmission submission(const ui::SchedulerPresentation& packet,quint64 request){
  require(packet&&packet->snapshot,"Real scheduler packet required");const auto& values=*packet->snapshot;
  ui::SchedulerSubmission out{packet->session,request,packet->revision,values.preferences,values.configuration.defaultUpload,values.configuration.defaultDownload,values.configuration.defaultsEnforce,{}};
  for(std::size_t i=0;i<values.configuration.schedules.size();++i){out.schedules.push_back({i,values.configuration.schedules[i]});}return out;
}
}
