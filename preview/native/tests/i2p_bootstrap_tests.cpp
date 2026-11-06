// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_bootstrap.hpp"
#include "generate_identity.hpp"
#include <openssl/evp.h>
#include <array>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
namespace i=biglybtc::services::i2p;
namespace p=i::provider;namespace s=biglybtc::storage;namespace fs=std::filesystem;
namespace {
void check(bool ok,const char* text){if(!ok){throw std::runtime_error(text);}}
template<class T>T take(std::expected<T,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
template<class T>T crypto(std::expected<T,i::Error> value){check(bool(value),"Generated identity crypto operation");return std::move(*value);}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);check(bool(in),"Read isolated bootstrap fixture");return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& path,std::string_view bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();check(bool(out)&&::chmod(path.c_str(),0600)==0,"Write isolated bootstrap fixture");}
std::string asString(i::View bytes){return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
i::View view(const std::string& bytes){return {reinterpret_cast<const i::Byte*>(bytes.data()),bytes.size()};}
constexpr std::string_view Challenge="BiglyBTC original I2P bootstrap signature roundtrip v1";
// Build key lengths rather than rely on a hand-written bencode fixture.
std::string config(){std::string out="d";for(const auto* name:{"PluginInfo.azneti2phelper.enabled","Plugin.azneti2phelper.enable","Plugin.azneti2phelper.azi2phelper.pure.enabled","Plugin.azneti2phelper.azi2phelper.mix.enabled","Plugin.azneti2phelper.azi2phelper.dht.enable"}){const std::string key=name;out+=std::to_string(key.size())+':'+key+"i1e";}return out+"6:future3:x"+std::string(1,'\0')+"ye";}
struct Area {
  fs::path root;std::optional<s::ProfileLease> lease;bool cleanup=true;
  explicit Area(std::optional<fs::path> requested={}){
    if(requested){root=*requested;check(!fs::exists(root)&&fs::create_directory(root),"Oracle requires a fresh isolated directory");cleanup=false;}
    else{std::string name="/tmp/biglybtc-i2p-bootstrap-XXXXXX";check(::mkdtemp(name.data()),"Create private bootstrap fixture");root=name;}
    check(::chmod(root.c_str(),0700)==0,"Private bootstrap fixture mode");write(root/"biglybt.config",config());write(root/"downloads.config","d9:downloadslee");lease.emplace(take(s::ProfileLease::acquire(root)));
  }
  ~Area(){lease.reset();if(cleanup){std::error_code error;fs::remove_all(root,error);}}
  fs::path plugin()const{return root/"plugins/azneti2phelper";}
  fs::path key(bool pure=true)const{return plugin()/(pure?"dest_key1.dat":"dest_key.dat");}
  fs::path properties(bool pure=true)const{return plugin()/(pure?"dht1.config":"dht.config");}
  void setting(const char* key,const std::string& value){auto d=biglybtc::core::Document::parse(read(root/"biglybt.config"));check(d&&d->replaceValue(key,value).has_value(),"Actual saved bootstrap setting");write(root/"biglybt.config",d->serialize());}
};
p::BootstrapOptions normal(bool pure=true){return {.mode=p::BootstrapMode::Normal,.key=pure?p::BootstrapKey::Pure:p::BootstrapKey::Mixed};}
using Snapshot=std::map<std::string,std::string>;
Snapshot files(const Area& area){Snapshot result;for(const auto& file:fs::recursive_directory_iterator(area.root)){
  const auto name=file.path().lexically_relative(area.root).string();struct stat st{};check(::lstat(file.path().c_str(),&st)==0,"Inspect bootstrap fixture");
  if(S_ISREG(st.st_mode)){const auto bytes=read(file.path());std::array<unsigned char,32> hash{};unsigned n=0;check(EVP_Digest(bytes.data(),bytes.size(),hash.data(),&n,EVP_sha256(),nullptr)==1&&n==32,"Fixture digest");result.emplace(name,std::to_string(st.st_ino)+':'+std::to_string(st.st_mode)+':'+std::to_string(st.st_mtim.tv_sec)+':'+std::to_string(st.st_mtim.tv_nsec)+':'+std::to_string(st.st_ctim.tv_sec)+':'+std::to_string(st.st_ctim.tv_nsec)+':'+std::string(reinterpret_cast<const char*>(hash.data()),hash.size()));}
  else if(S_ISLNK(st.st_mode)){result.emplace(name,"symlink:"+fs::read_symlink(file.path()).string());}
}return result;}
void generationAndHealthyProfiles(){
  auto one=crypto(i::generateClientIdentity()),two=crypto(i::generateClientIdentity());check(one.valid()&&two.valid()&&one.destination()->hash!=two.destination()->hash&&one.keyStream().size()==679,"OpenSSL identity generation/format");
  check(one.destination()->signingType==i::SigningType::Ed25519&&one.destination()->encryptionType==0,"Generated unsupported key type");
  const auto signature=crypto(i::signData(i::SigningType::Ed25519,one.signingPrivateKey(),view(std::string(Challenge))));check(bool(i::verifyData(i::SigningType::Ed25519,one.destination()->signingPublicKey,signature,view(std::string(Challenge)))),"Generated signature does not verify");
  Area a;const auto saved=read(a.root/"biglybt.config");const auto pure=take(p::bootstrapI2pProfile(*a.lease,normal()));
  check(pure.keyCreated&&pure.configCreated&&pure.discoveryEnabled&&pure.nodeId&&pure.queryPort>=10000&&pure.queryPort<=65532&&i::dht::validContact({*pure.nodeId,pure.destination.hash,pure.queryPort,pure.destination}),"Actual generated key/NID/port binding");
  const auto before=files(a);const auto epoch=a.lease->sourceRevision();const auto again=take(p::bootstrapI2pProfile(*a.lease,normal()));check(!again.keyCreated&&!again.configCreated&&again.nodeId==pure.nodeId&&again.destination.hash==pure.destination.hash&&files(a)==before&&a.lease->sourceRevision()==epoch,"Valid original identity was rewritten or advanced source epoch");
  const auto mixed=take(p::bootstrapI2pProfile(*a.lease,normal(false)));check(mixed.destination.hash!=pure.destination.hash&&mixed.keyCreated&&mixed.configCreated&&read(a.root/"biglybt.config")==saved,"Index0/1 isolation or original unknown config changed");
  struct stat st{};for(const auto& file:{a.key(),a.properties(),a.key(false),a.properties(false)}){check(::stat(file.c_str(),&st)==0&&(st.st_mode&07777)==0600&&st.st_nlink==1,"Published identity/config was not private single-link data");}
  check(!fs::exists(a.plugin()/".BiglyBTC.i2p-bootstrap1"),"Healthy bootstrap marker not cleaned");
  check(!s::ProfileLease::acquire(a.root),"Bootstrap dropped original exclusion");a.lease.reset();auto released=s::ProfileLease::acquire(a.root);check(bool(released),"Bootstrap leaked an OFD owner");
}
void disabledPreviewAndMissingConfig(){
  for(const auto* flag:{"PluginInfo.azneti2phelper.enabled","Plugin.azneti2phelper.enable","Plugin.azneti2phelper.azi2phelper.pure.enabled"}){
    Area a;a.setting(flag,"i0e");const auto before=files(a);auto denied=p::bootstrapI2pProfile(*a.lease,normal());check(!denied&&denied.error().code==s::ErrorCode::Unsupported&&files(a)==before&&!fs::exists(a.root/"plugins"),"Disabled plugin/index wrote bootstrap files");
  }
  {Area a;const auto before=files(a);check(!p::bootstrapI2pProfile(*a.lease)&&files(a)==before&&!fs::exists(a.root/"plugins"),"Preview wrote identity files");std::stop_source stop;stop.request_stop();check(!p::bootstrapI2pProfile(*a.lease,normal(),stop.get_token())&&files(a)==before,"Pre-cancelled bootstrap wrote files");check(!p::bootstrapI2pProfile(*a.lease,normal(),{},std::chrono::steady_clock::time_point::min())&&files(a)==before,"Expired bootstrap wrote files");}
  {Area a;a.setting("Plugin.azneti2phelper.enable","3:bad");const auto before=files(a);check(!p::bootstrapI2pProfile(*a.lease,normal())&&files(a)==before,"Malformed plugin flag created an identity");}
  {Area a;a.setting("Plugin.azneti2phelper.azi2phelper.dht.enable","i0e");const auto configBefore=read(a.root/"biglybt.config");auto keyOnly=take(p::bootstrapI2pProfile(*a.lease,normal()));check(keyOnly.keyCreated&&!keyOnly.configCreated&&!keyOnly.discoveryEnabled&&!keyOnly.nodeId&&!fs::exists(a.properties())&&read(a.root/"biglybt.config")==configBefore,"DHT-disabled bootstrap activated or rewrote discovery");const auto key=read(a.key());a.setting("Plugin.azneti2phelper.azi2phelper.dht.enable","i1e");auto withConfig=take(p::bootstrapI2pProfile(*a.lease,normal()));check(!withConfig.keyCreated&&withConfig.configCreated&&read(a.key())==key,"Missing config rotated existing key");}
  {Area a;take(p::bootstrapI2pProfile(*a.lease,normal()));const auto key=read(a.key());fs::remove(a.properties());auto restored=take(p::bootstrapI2pProfile(*a.lease,normal()));check(!restored.keyCreated&&restored.configCreated&&read(a.key())==key,"Original key-only profile was not preserved");const auto properties=read(a.properties());write(a.properties(),properties+"future.setting=preserved\n");const auto stable=files(a);take(p::bootstrapI2pProfile(*a.lease,normal()));check(files(a)==stable,"Existing valid unknown properties were rewritten");}
}
void foreignAmbiguousAndUnsafeInputs(){
  for(unsigned kind=0;kind<8;++kind){Area a;take(p::bootstrapI2pProfile(*a.lease,normal()));
    if(kind==0){fs::remove(a.key());}
    if(kind==1){write(a.properties(),read(a.properties())+"port=12345\n");}
    if(kind==2){auto properties=read(a.properties());const auto at=properties.find("nid=");check(at!=std::string::npos,"Generated secure NID field");properties[at+4]=properties[at+4]=='a'?'b':'a';write(a.properties(),properties);}
    if(kind==3){write(a.plugin()/"dest_key1.dat.saving","ambiguous recovery");}
    if(kind==4){check(::chmod(a.key().c_str(),0644)==0,"Fixture unsafe key mode");}
    if(kind==5){fs::rename(a.key(),a.plugin()/"held-old-key");check(::symlink("held-old-key",a.key().c_str())==0,"Fixture key symlink");}
    if(kind==6){write(a.key(),"truncated private identity");}
    if(kind==7){check(::link(a.key().c_str(),(a.plugin()/"hardlinked-key").c_str())==0,"Fixture key hardlink");}
    const auto before=files(a);auto refused=p::bootstrapI2pProfile(*a.lease,normal());check(!refused&&files(a)==before,"Ambiguous/unsafe original identity was overwritten");
  }
  {Area a;take(p::bootstrapI2pProfile(*a.lease,normal()));const auto config=read(a.properties());write(a.properties(),config+"n\\id=hidden\n");const auto before=files(a);check(!p::bootstrapI2pProfile(*a.lease,normal())&&files(a)==before,"Escaped identity keys were guessed or replaced");}
  {Area a;take(p::bootstrapI2pProfile(*a.lease,normal()));fs::rename(a.root/".azlock",a.root/"old-lock");write(a.root/".azlock","");const auto before=files(a);check(!p::bootstrapI2pProfile(*a.lease,normal())&&files(a)==before,"Replaced OFD lock was accepted");}
}
p::BootstrapStage crashStage=p::BootstrapStage::Completed;
void crash(p::BootstrapStage stage){if(stage==crashStage){::_exit(73);}}
std::stop_source* cancelling=nullptr;
void cancel(p::BootstrapStage stage){if(stage==p::BootstrapStage::KeyPublished){cancelling->request_stop();}}
Area* externalEdit=nullptr;
void editConfiguration(p::BootstrapStage stage){if(stage==p::BootstrapStage::JournalSynced){externalEdit->setting("future","8:external");}}
void partialBootstrapIsPreservedReview(){
  for(const auto stage:{p::BootstrapStage::JournalSynced,p::BootstrapStage::KeyTemporarySynced,p::BootstrapStage::KeyPublished,p::BootstrapStage::ConfigTemporarySynced,p::BootstrapStage::ConfigPublished}){
    Area a;crashStage=stage;const auto parent=::getpid();const auto pid=::fork();check(pid>=0,"Fork real bootstrap crash prefix");
    if(pid==0){if(::prctl(PR_SET_PDEATHSIG,SIGKILL,0,0,0)!=0||::getppid()!=parent){::_exit(75);}auto value=p::bootstrapI2pProfile(*a.lease,normal(),{},std::chrono::steady_clock::now()+std::chrono::seconds{5},crash);static_cast<void>(value);::_exit(74);}
    int status=0;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    for(;;){const auto joined=::waitpid(pid,&status,WNOHANG);if(joined==pid){break;}check(joined==0,"Wait real bootstrap child");if(std::chrono::steady_clock::now()>=end){::kill(pid,SIGKILL);::waitpid(pid,&status,0);throw std::runtime_error("Bootstrap crash child deadline");}std::this_thread::sleep_for(std::chrono::milliseconds{5});}
    check(WIFEXITED(status)&&WEXITSTATUS(status)==73,"Real bootstrap crash prefix not reached");const auto before=files(a);auto refused=p::bootstrapI2pProfile(*a.lease,normal());check(!refused&&refused.error().code==s::ErrorCode::Conflict&&files(a)==before&&fs::exists(a.plugin()/".BiglyBTC.i2p-bootstrap1"),"Partial identity was replayed/replaced instead of preserved review");
  }
  {Area a;std::stop_source source;cancelling=&source;auto result=p::bootstrapI2pProfile(*a.lease,normal(),source.get_token(),std::chrono::steady_clock::now()+std::chrono::seconds{5},cancel);cancelling=nullptr;check(!result&&result.error().code==s::ErrorCode::Cancelled&&fs::exists(a.key())&&!fs::exists(a.properties())&&fs::exists(a.plugin()/".BiglyBTC.i2p-bootstrap1"),"Partial cancellation claimed complete bootstrap");}
  {Area a;externalEdit=&a;auto result=p::bootstrapI2pProfile(*a.lease,normal(),{},std::chrono::steady_clock::now()+std::chrono::seconds{5},editConfiguration);externalEdit=nullptr;auto configuration=biglybtc::core::Document::parse(read(a.root/"biglybt.config"));check(!result&&result.error().code==s::ErrorCode::Conflict&&configuration&&configuration->find("future")->bytes=="external"&&!fs::exists(a.key())&&fs::exists(a.plugin()/".BiglyBTC.i2p-bootstrap1"),"Foreign configuration change was overwritten or authorized publication");}
}
void emitInterop(const fs::path& root){
  Area a(root);
  for(const bool pure:{false,true}){const auto result=take(p::bootstrapI2pProfile(*a.lease,normal(pure)));const auto keyBytes=read(a.key(pure));auto identity=crypto(i::decodeClientIdentity(view(keyBytes)));const std::string label=pure?"pure":"mixed";
    write(root/(label+".destination"),asString(result.destination.wire));const auto data=std::string(Challenge);write(root/"challenge",data);
    const auto signature=crypto(i::signData(identity.destination()->signingType,identity.signingPrivateKey(),view(data)));write(root/(label+".native.sig"),asString(signature));
  }
}
void verifyInterop(const fs::path& root){
  for(const auto* label:{"mixed","pure"}){const bool pure=std::string_view(label)=="pure";const auto key=read(root/"plugins/azneti2phelper"/(pure?"dest_key1.dat":"dest_key.dat"));check(read(root/(std::string(label)+".java.keys"))==key,"Actual original Java key reader/writer roundtrip differs");auto identity=crypto(i::decodeClientIdentity(view(key)));const auto data=read(root/"challenge"),signature=read(root/(std::string(label)+".java.sig"));check(bool(i::verifyData(identity.destination()->signingType,identity.destination()->signingPublicKey,view(signature),view(data))),"Actual original Java signature did not verify natively");}
}
}
int main(int argc,char** argv){try{
  if(argc==3&&std::string_view(argv[1])=="--emit-interop"){emitInterop(argv[2]);}
  else if(argc==3&&std::string_view(argv[1])=="--verify-interop"){verifyInterop(argv[2]);}
  else if(argc==1){generationAndHealthyProfiles();disabledPreviewAndMissingConfig();foreignAmbiguousAndUnsafeInputs();partialBootstrapIsPreservedReview();}
  else{throw std::runtime_error("Unsupported bootstrap test arguments");}
  std::cout<<"PASS original-compatible offline I2P bootstrap\n";return 0;
}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
