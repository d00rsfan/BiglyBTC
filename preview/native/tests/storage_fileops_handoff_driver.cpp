#include "file_operations.hpp"
#include "biglybtc/library.hpp"
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace {
using namespace biglybtc;namespace fs=std::filesystem;
void require(bool yes,const char* why){if(!yes){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,storage::Error> result){if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);}
QString text(const fs::path& value){return QString::fromStdString(value.string());}
bool inside(const fs::path& root,const fs::path& path){auto a=root.begin(),b=path.begin();for(;a!=root.end();++a,++b){if(b==path.end()||*a!=*b){return false;}}return b!=path.end();}
void owned(const fs::path& path,bool directory){require(path.is_absolute()&&path.lexically_normal()==path&&fs::canonical(path)==path,"Only canonical generated paths");struct stat s{};require(::lstat(path.c_str(),&s)==0&&s.st_uid==::geteuid()&&(directory?S_ISDIR(s.st_mode):S_ISREG(s.st_mode)&&s.st_nlink==1&&s.st_size>=0&&s.st_size<=4*1024*1024),"Unsafe generated path type/ownership");}
std::string bytes(const fs::path& path){owned(path,false);const int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);require(fd>=0,"Cannot read generated file");std::string value;std::array<char,65536> buffer{};for(;;){const auto n=::read(fd,buffer.data(),buffer.size());if(n<0&&errno==EINTR){continue;}if(n<0){::close(fd);throw std::runtime_error("Generated read failed");}if(!n){break;}value.append(buffer.data(),static_cast<std::size_t>(n));}require(::close(fd)==0,"Generated close failed");return value;}
QString digest(const fs::path& path){const auto data=bytes(path);return QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(data.data(),static_cast<qsizetype>(data.size())),QCryptographicHash::Sha256).toHex());}
QJsonObject controls(const fs::path& profile,const core::TorrentHash& hash){QJsonObject result;const auto parent=profile/"active"/hash.hex();for(unsigned index=0;index<3;++index){for(const auto* ending:{".dat",".dat.bak",".dat.2",".dat.2.bak"}){const auto leaf=parent/("fmfile"+std::to_string(index)+ending);const auto status=fs::symlink_status(leaf);if(status.type()==fs::file_type::not_found){continue;}owned(leaf,false);result[text(leaf.lexically_relative(profile))]=digest(leaf);}}return result;}
void output(const fs::path& path,const QJsonObject& object){const auto data=QJsonDocument(object).toJson(QJsonDocument::Indented);const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);require(fd>=0,"Fresh receipt required");qsizetype offset=0;while(offset<data.size()){const auto n=::write(fd,data.constData()+offset,static_cast<std::size_t>(data.size()-offset));if(n<0&&errno==EINTR){continue;}if(n<=0){::close(fd);throw std::runtime_error("Cannot write receipt");}offset+=n;}const bool synced=::fsync(fd)==0;const bool closed=::close(fd)==0;require(synced&&closed,"Cannot sync receipt");}
}
int main(int argc,char** argv){
  if(argc!=5){std::cerr<<"Usage: storage_fileops_handoff_driver WORK_ROOT PROFILE DESTINATION OUTPUT_JSON\n";return 1;}
  try{
    const fs::path root(argv[1]),profile(argv[2]),destination(argv[3]),receipt(argv[4]);owned(root,true);owned(profile,true);
    require(root.parent_path()==fs::path("/tmp")&&root.filename().string().starts_with("biglybtc-managed-handoff-storage-fileops-"),"Fresh dedicated generated handoff root required");struct stat privateRoot{};require(::lstat(root.c_str(),&privateRoot)==0&&(privateRoot.st_mode&077)==0,"Private owned handoff root required");
    require(bytes(root/".storage-fileops-fixture")=="cf01-owned-nonlinear-move\n","Runner-owned explicit fixture marker required");
    const auto caseRoot=profile.parent_path();require(profile.filename()=="profile"&&caseRoot.parent_path()==root&&(caseRoot.filename()=="profile-2"||caseRoot.filename()=="profile-3"||caseRoot.filename()=="profile-4"),"Only generated storage profile cases");
    const auto payload=caseRoot/"payload";owned(payload,true);require(destination==payload/"moved-before"||destination==payload/"moved-after","Only the two exact owned relocation parents");require(!fs::exists(destination)&&!fs::is_symlink(fs::symlink_status(destination)),"Fresh destination parent required");require(receipt.parent_path()==caseRoot&&receipt.lexically_normal()==receipt&&!fs::exists(receipt),"Fresh receipt inside exact case required");
    // Creating this explicitly selected empty parent is a fixture setup effect.
    require(fs::create_directory(destination),"Cannot create generated move parent");fs::permissions(destination,fs::perms::owner_all);
    const auto config=bytes(profile/"biglybt.config");std::optional<storage::ProfileLease> lease;lease.emplace(take(storage::ProfileLease::acquire(profile)));
    auto inventory=take(storage::ConfigFile::load(*lease,"downloads.config"));auto library=core::readLibrary(inventory.document());require(library&&library->issues.empty()&&library->downloads.size()==1&&library->downloads[0].hash.has_value(),"One actual original saved torrent required");const auto hash=*library->downloads[0].hash;const auto archive=fs::path(library->downloads[0].torrentFile);owned(archive,false);require(inside(caseRoot,archive),"Original archive outside owned case");const auto archiveHash=digest(archive);
    const auto beforeControls=controls(profile,hash);auto prepared=take(storage::prepareFileOperation(*lease,{hash,storage::FileOperationKind::Relocate,destination}));const auto preview=prepared.preview();require(preview.files.size()==3&&inside(payload,preview.sourceRoot)&&preview.targetRoot==destination/"layout","Exact generated nonlinear move scope required");
    QJsonArray files;for(const auto& entry:preview.files){require(entry.present&&entry.index<3&&inside(payload,entry.source)&&inside(destination,entry.destination),"Move entry escaped generated payload");owned(entry.source,false);files.append(QJsonObject{{"index",static_cast<qint64>(entry.index)},{"source",text(entry.source)},{"destination",text(entry.destination)},{"physical_bytes",static_cast<qint64>(entry.actualBytes)},{"logical_bytes",static_cast<qint64>(entry.logicalBytes)},{"sha256",digest(entry.source)},{"device",QString::number(static_cast<qulonglong>(entry.device))},{"inode",QString::number(static_cast<qulonglong>(entry.inode))}});}
    const auto confirmation=take(storage::confirmFileOperation(prepared,preview.approvalScopeDigest,false));const auto moved=take(storage::applyFileOperation(*lease,std::move(prepared),confirmation));require(moved.filesMoved==3&&moved.filesDeleted==0&&!storage::hasPendingFileOperation(*lease),"Move did not finish exact generated scope");
    for(const auto& value:files){const auto row=value.toObject();const auto from=fs::path(row["source"].toString().toStdString()),to=fs::path(row["destination"].toString().toStdString());require(!fs::exists(from)&&digest(to)==row["sha256"].toString(),"Relocation changed physical payload bytes");}
    const auto afterControls=controls(profile,hash);require(beforeControls==afterControls&&digest(archive)==archiveHash&&bytes(profile/"biglybt.config")==config,"Move changed profile-anchored control/archive/config bytes");
    lease.reset();{auto released=take(storage::ProfileLease::acquire(profile));require(released.valid(),"Move lease was not released");}
    output(receipt,QJsonObject{{"hash",QString::fromStdString(hash.hex())},{"source_root",text(preview.sourceRoot)},{"target_root",text(preview.targetRoot)},{"files_moved",static_cast<qint64>(moved.filesMoved)},{"files",files},{"controls_before",beforeControls},{"controls_after",afterControls},{"config_unchanged",true},{"archive_sha256",archiveHash},{"lease_reacquired_after_release",true},{"engine_started",false}});
    std::cout<<"STORAGE_MOVE_FINISHED "<<hash.hex()<<" files=3 controls="<<beforeControls.size()<<" lease-released\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
