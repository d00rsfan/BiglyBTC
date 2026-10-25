#include "biglybtc/queue_policy.hpp"
#include "application_settings.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
namespace fs = std::filesystem;
using Id = core::SettingId;
using Change = core::SettingChange;
using State = storage::SettingState;
std::size_t checks = 0, families = 0;
void check(bool value, const char* message) { ++checks; if (!value) { throw std::runtime_error(message); } }
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
template<class T> T take(std::expected<T,storage::Error> value) {
  if (!value) { throw std::runtime_error(value.error().message); } return std::move(*value);
}
template<class F> void run(const char* name, F&& function) {
  try { function(); ++families; } catch (const std::exception& error) { throw std::runtime_error(std::string(name)+": "+error.what()); }
}
std::string b(std::string_view value) { return std::to_string(value.size())+':'+std::string(value); }
void write(const fs::path& path,std::string_view value) {
  const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600); CHECK(fd>=0);
  std::size_t at=0;
  while(at<value.size()) { const auto n=::write(fd,value.data()+at,value.size()-at); if(n<0&&errno==EINTR){continue;} if(n<=0){::close(fd);throw std::runtime_error("fixture write");} at+=static_cast<std::size_t>(n); }
  CHECK(::close(fd)==0);
}
std::string read(const fs::path& path) {
  const int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK); CHECK(fd>=0);
  std::string result; std::array<char,4096> buffer{};
  for(;;) { const auto n=::read(fd,buffer.data(),buffer.size()); if(n<0&&errno==EINTR){continue;} if(n<0){::close(fd);throw std::runtime_error("fixture read");} if(!n){break;} result.append(buffer.data(),static_cast<std::size_t>(n)); }
  CHECK(::close(fd)==0); return result;
}
struct Area {
  fs::path root; std::optional<storage::ProfileLease> lease;
  Area() { char name[]="/tmp/biglybtc-options-XXXXXX"; const auto* path=::mkdtemp(name); CHECK(path); root=path; lease.emplace(take(storage::ProfileLease::acquire(root))); }
  ~Area(){lease.reset();std::error_code error;fs::remove_all(root,error);}
  Area(const Area&)=delete;Area& operator=(const Area&)=delete;
  fs::path primary()const{return root/"biglybt.config";}
  storage::ApplicationSettingsSnapshot snapshot(){return take(storage::readApplicationSettings(*lease));}
};
storage::PreparedApplicationSettings prepare(const storage::ApplicationSettingsSnapshot& baseline,std::initializer_list<Change> changes) {
  return take(storage::prepareApplicationSettings(baseline,std::span(changes.begin(),changes.size())));
}
const storage::ApplicationSetting& field(const storage::ApplicationSettingsSnapshot& snapshot,Id id) { return snapshot.fields.at(static_cast<std::size_t>(id)); }
std::string raw(const core::Document& document,std::string_view key) {
  const auto* node=document.find(key); CHECK(node);return document.originalBytes().substr(node->begin,node->end-node->begin);
}
bool failDirectory=false;std::stop_source* cancelOnPublish=nullptr;
void observer(storage::SaveStage stage) {
  if(stage==storage::SaveStage::PrimaryRenamed) { if(cancelOnPublish){cancelOnPublish->request_stop();} else{failDirectory=true;} }
}
} // namespace
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
  struct stat info{};
  if(failDirectory&&::fstat(fd,&info)==0&&S_ISDIR(info.st_mode)){failDirectory=false;errno=EIO;return -1;}
  return __real_fsync(fd);
}
int main() {
  try {
    run("catalog original literals and field constraints",[]{
      CHECK(core::applicationSettingsCatalog().size()==101);
      for(const auto& item:core::applicationSettingsCatalog()) {
        CHECK(core::applicationSetting(item.id)==&item); CHECK(!item.key.empty()&&!item.label.empty());
        const auto value=core::applicationSettingDefault(item); if(value){CHECK(core::validApplicationSettingValue(item,*value));}
      }
      CHECK(std::get<std::int64_t>(*core::applicationSettingDefault(*core::applicationSetting(Id::TcpPort)))==6881);
      CHECK(std::get<std::int64_t>(*core::applicationSettingDefault(*core::applicationSetting(Id::MaxActive)))==4);
      CHECK(core::applicationSetting(Id::ProxyPassword)->secret);
      CHECK(std::get<bool>(*core::applicationSettingDefault(*core::applicationSetting(Id::LanRates))));
      CHECK(core::applicationSetting(Id::RenameIncomplete)->applicability==core::SettingsApplicability::OriginalOnly);
      CHECK(!core::applicationSettingDefault(*core::applicationSetting(Id::DefaultSavePath)));
      CHECK(!core::applicationSetting(static_cast<Id>(999)));
    });
    run("missing and explicit default no-op never materialize config",[]{
      Area area;const auto snapshot=area.snapshot();CHECK(snapshot.missing());CHECK(snapshot.fields.size()==101);
      CHECK(field(snapshot,Id::UploadLimit).state==State::Defaulted);
      CHECK(field(snapshot,Id::DefaultSavePath).state==State::UnavailableDefault);
      const auto desired=prepare(snapshot,{{Id::UploadLimit,std::int64_t{0}},{Id::TcpEnabled,true}});CHECK(!desired.changed());
      const auto result=take(storage::saveApplicationSettings(*area.lease,snapshot,desired));CHECK(result.missing());
      CHECK(!fs::exists(area.primary())&&!fs::exists(area.root/"biglybt.config.bak"));
    });
    run("first sparse creation and exact original filename",[]{
      Area area;write(area.root/"azureus.config","d6:opaque3:olde");const auto before=area.snapshot();CHECK(before.missing());
      const auto desired=prepare(before,{{Id::UploadLimit,std::int64_t{256}},{Id::DefaultSavePath,std::string("/owned/downloads")}});
      CHECK(desired.changed());const auto result=take(storage::saveApplicationSettings(*area.lease,before,desired));
      CHECK(result.source&&result.recoverySource==storage::RecoverySource::Primary);
      CHECK(std::get<std::int64_t>(*field(result,Id::UploadLimit).value)==256);
      CHECK(!result.source->find("TCP.Listen.Port"));CHECK(!result.source->find("Proxy.Password"));
      CHECK(read(area.root/"azureus.config")=="d6:opaque3:olde");CHECK(!fs::exists(area.root/"biglybt.config.bak"));
      struct stat mode{};CHECK(::stat(area.primary().c_str(),&mode)==0);CHECK((mode.st_mode&0777)==0600);
    });
    run("sparse save preserves raw unknown and unedited malformed fields",[]{
      Area area;const std::string opaque="l"+b(std::string("\0\xffX",3))+"i-7ee";
      const auto bytes="d"+b("opaque")+opaque+b("TCP.Listen.Port")+b("wrong")+b("Max Upload Speed KBs")+"i12ee";
      write(area.primary(),bytes);const auto before=area.snapshot();CHECK(field(before,Id::TcpPort).state==State::InvalidType);
      CHECK(!field(before,Id::TcpPort).value);const auto desired=prepare(before,{{Id::UploadLimit,std::int64_t{33}}});
      const auto after=take(storage::saveApplicationSettings(*area.lease,before,desired));
      CHECK(raw(*after.source,"opaque")==opaque);CHECK(raw(*after.source,"TCP.Listen.Port")==b("wrong"));
      CHECK(read(area.root/"biglybt.config.bak")==bytes);
      const auto repaired=prepare(after,{{Id::TcpPort,std::int64_t{12345}}});
      const auto fixed=take(storage::saveApplicationSettings(*area.lease,after,repaired));CHECK(field(fixed,Id::TcpPort).state==State::Stored);
    });
    run("invalid values are visible and rejected edits are atomic",[]{
      Area area;write(area.primary(),"d"+b("TCP.Listen.Port.Enable")+"i2e"+b("Max Upload Speed KBs")+"i-1e"+b("Proxy.Port")+"i80ee");
      const auto snapshot=area.snapshot();CHECK(field(snapshot,Id::TcpEnabled).state==State::OutOfRange);
      CHECK(field(snapshot,Id::UploadLimit).state==State::OutOfRange);CHECK(std::get<std::int64_t>(*field(snapshot,Id::UploadLimit).value)==-1);
      CHECK(field(snapshot,Id::ProxyPort).state==State::InvalidType);const auto bytes=read(area.primary());
      for(const auto& bad:std::array<Change,7>{{{Id::TcpPort,true},{Id::TcpPort,std::int64_t{0}},
          {Id::ProxyPort,std::string("65536")},{Id::PeerProxyVersion,std::string("V6")},
          {Id::DefaultSavePath,std::string("/a/../b")},{Id::ProxyPassword,std::string("a\0b",3)},
          {static_cast<Id>(999),false}}}) {
        const std::array changes{Change{Id::DownloadLimit,std::int64_t{17}},bad};
        CHECK(!storage::prepareApplicationSettings(snapshot,changes));CHECK(read(area.primary())==bytes);
      }
      CHECK(!storage::prepareApplicationSettings(snapshot,std::array{Change{Id::UploadLimit,std::int64_t{1}},Change{Id::UploadLimit,std::int64_t{2}}}));
    });
    run("whole snapshot CAS checks unknown fields even on no-op",[]{
      Area area;write(area.primary(),"d6:opaquei1ee");const auto before=area.snapshot();const auto noop=prepare(before,{});
      const auto changed=prepare(before,{{Id::DownloadLimit,std::int64_t{4}}});write(area.primary(),"d6:opaquei2ee");
      const auto a=storage::saveApplicationSettings(*area.lease,before,changed);CHECK(!a&&a.error().code==storage::ErrorCode::Conflict);
      const auto b=storage::saveApplicationSettings(*area.lease,before,noop);CHECK(!b&&b.error().code==storage::ErrorCode::Conflict);
      CHECK(read(area.primary())=="d6:opaquei2ee");CHECK(!fs::exists(area.root/"biglybt.config.bak"));
    });
    run("recovery readable but never silently writable",[]{
      Area area;write(area.primary(),"incomplete");write(area.root/"biglybt.config.bak","d6:opaquei8ee");
      const auto snapshot=area.snapshot();CHECK(snapshot.recoverySource==storage::RecoverySource::Backup);
      const auto desired=prepare(snapshot,{{Id::MaxActive,std::int64_t{2}}});
      const auto result=storage::saveApplicationSettings(*area.lease,snapshot,desired);CHECK(!result&&result.error().code==storage::ErrorCode::Conflict);
      CHECK(read(area.primary())=="incomplete");CHECK(read(area.root/"biglybt.config.bak")=="d6:opaquei8ee");
      CHECK(::unlink(area.primary().c_str())==0);CHECK(::unlink((area.root/"biglybt.config.bak").c_str())==0);
      write(area.root/"biglybt.config.saving","malformed");CHECK(!storage::readApplicationSettings(*area.lease));
      CHECK(read(area.root/"biglybt.config.saving")=="malformed");
    });
    run("original unsafe path and transaction errors retained",[]{
      Area area;write(area.primary(),"de");const auto before=area.snapshot();const auto desired=prepare(before,{{Id::UploadLimit,std::int64_t{8}}});
      write(area.root/"biglybt.config.saving","owned interrupted bytes");
      auto original=take(storage::ConfigFile::load(*area.lease,"biglybt.config",storage::ApplicationSettingsLimits));
      CHECK(original.document().replaceValue("Max Upload Speed KBs","i8e"));const auto expected=original.save(*area.lease);CHECK(!expected);
      const auto result=storage::saveApplicationSettings(*area.lease,before,desired);CHECK(!result);
      CHECK(result.error().code==expected.error().code&&result.error().message==expected.error().message);
      CHECK(read(area.primary())=="de"&&read(area.root/"biglybt.config.saving")=="owned interrupted bytes");
      CHECK(::unlink((area.root/"biglybt.config.saving").c_str())==0);CHECK(::unlink(area.primary().c_str())==0);
      write(area.root/"other","de");CHECK(::symlink("other",area.primary().c_str())==0);
      const auto unsafe=storage::readApplicationSettings(*area.lease);CHECK(!unsafe&&unsafe.error().code==storage::ErrorCode::UnsafePath);
    });
    run("publication sync error remains error with actual desired primary",[]{
      Area area;write(area.primary(),"de");const auto before=area.snapshot();const auto desired=prepare(before,{{Id::DownloadLimit,std::int64_t{7}}});
      const auto result=storage::saveApplicationSettings(*area.lease,before,desired,{},observer);
      CHECK(!result&&result.error().code==storage::ErrorCode::Io);CHECK(result.error().message.find("already occurred")!=std::string::npos);
      const auto actual=area.snapshot();CHECK(std::get<std::int64_t>(*field(actual,Id::DownloadLimit).value)==7);
      CHECK(read(area.root/"biglybt.config.bak")=="de");
    });
    run("cancellation only before synchronous commit",[]{
      Area area;const auto before=area.snapshot();const auto desired=prepare(before,{{Id::UploadLimit,std::int64_t{9}}});
      std::stop_source stopped;stopped.request_stop();const auto rejected=storage::saveApplicationSettings(*area.lease,before,desired,stopped.get_token());
      CHECK(!rejected&&rejected.error().code==storage::ErrorCode::Cancelled);CHECK(!fs::exists(area.primary()));
      std::stop_source after;cancelOnPublish=&after;
      const auto result=storage::saveApplicationSettings(*area.lease,before,desired,after.get_token(),observer);cancelOnPublish=nullptr;
      CHECK(result&&after.stop_requested());CHECK(std::get<std::int64_t>(*field(*result,Id::UploadLimit).value)==9);
    });
    run("bounded documents edited public snapshots and foreign prepared tokens reject",[]{
      Area area;write(area.primary(),std::string(storage::ApplicationSettingsLimits.inputBytes+1,'x'));CHECK(!storage::readApplicationSettings(*area.lease));
      write(area.primary(),"de");auto before=area.snapshot();const auto desired=prepare(before,{{Id::MaxDownloads,std::int64_t{2}}});
      auto other=area.snapshot();CHECK(!storage::saveApplicationSettings(*area.lease,other,desired));
      auto edited=core::Document::parse("de");CHECK(edited&&edited->replaceValue("opaque","i1e"));
      before.source=std::make_shared<const core::Document>(std::move(*edited));CHECK(!storage::prepareApplicationSettings(before,{}));
      CHECK(read(area.primary())=="de");
    });
    run("classic FP default and sparse float queue settings",[]{
      Area area;write(area.primary(),"d2:ui3:az27:unknownli7e3:rawee");
      const auto before=read(area.primary());const auto snapshot=area.snapshot();
      CHECK(!std::get<bool>(*field(snapshot,Id::FirstPriorityIgnoreZeroPeers).value));
      CHECK(read(area.primary())==before);
      CHECK(core::applicationSetting(Id::MinDownloads)->applicability==core::SettingsApplicability::Live);
      const auto edited=prepare(snapshot,{{Id::RankMode,std::int64_t{3}},
          {Id::IgnoreShareRatio,std::string("1.25")},{Id::FirstPriorityIgnoreZeroPeers,true}});
      const auto saved=take(storage::saveApplicationSettings(*area.lease,snapshot,edited));
      const auto policy=core::readQueuePolicy(saved.source.get());CHECK(policy);
      CHECK(policy->rankMode==core::QueueRankMode::Timed);CHECK(policy->ignoreShareRatio==1250);
      CHECK(policy->firstPriorityIgnoreZeroPeers);
      CHECK(read(area.primary()).find("7:unknownli7e3:rawe")!=std::string::npos);
      CHECK(read(area.root/"biglybt.config.bak")==before);
    });
    std::cout<<families<<" application-settings families, "<<checks<<" assertions passed\n";
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
