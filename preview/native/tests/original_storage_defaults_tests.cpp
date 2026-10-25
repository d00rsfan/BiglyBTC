#include "original_storage_defaults.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace fs=std::filesystem;
using namespace biglybtc;
using Kind=storage::OriginalStorageKind;
std::size_t assertions=0,cases=0;
void check(bool value,const char* why){++assertions;if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,storage::Error>&& value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
core::Document doc(std::string raw){auto d=core::Document::parse(std::move(raw),storage::compatibleConfigLimits());check(d.has_value(),"fixture parse");return std::move(*d);}
std::string b(std::string_view s){return std::to_string(s.size())+':'+std::string(s);}
void put(core::Document& d,std::string_view key,std::string raw){check(d.replaceValue(key,std::move(raw)).has_value(),"fixture edit");}
void write(const fs::path& p,std::string_view bytes){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(f),"fixture write");}
struct Fixture {
  fs::path root,profile;
  storage::ProfileLease lease;
  core::TorrentHash hash;
  static fs::path area(){char p[]="/tmp/biglybtc-save-defaults-XXXXXX";auto* made=::mkdtemp(p);check(made!=nullptr,"owned area");return made;}
  static storage::ProfileLease held(const fs::path& p){fs::create_directory(p);return take(storage::ProfileLease::acquire(p));}
  Fixture():root(area()),profile(root/"profile"),lease(held(profile)),hash(take(core::TorrentHash::parse(std::string(20,'x')).transform_error([](auto){return storage::Error{storage::ErrorCode::InvalidFormat,"fixture hash"};}))){}
  ~Fixture(){std::error_code ignored;fs::remove_all(root,ignored);}
  compat::OriginalLayout layout(bool multiple=false)const{return {multiple?"bundle":"file.bin",multiple,16384,32768,{{"file.bin",0,32768,false}}};}
  core::SavedDownload saved(const fs::path& p,std::optional<std::string> leaf={})const{core::SavedDownload d;d.saveDirectory=p.string();d.saveFile=std::move(leaf);d.legacySavePath=!d.saveFile;return d;}
  fs::path control(std::size_t i,Kind kind)const{return profile/"active"/hash.hex()/storage::originalControlLeaf(i,kind);}
  storage::ResolvedOriginalStorageTypes types(const compat::OriginalLayout& layout,const core::Document& attrs){return take(storage::resolveOriginalStorageTypes(lease,hash,layout,attrs.root()));}
};
void run(const char* name,const std::function<void()>& test){test();++cases;std::cout<<"case "<<name<<'\n';}
}
int main(){try{
  run("simple-existing-directory-file-and-missing",[]{Fixture f;const auto layout=f.layout();fs::create_directory(f.root/"downloads");write(f.root/"actual.bin","actual");
    const auto directory=take(storage::resolveOriginalSaveRoot(f.saved(f.root/"downloads"),layout));check(directory.saveDirectory==(f.root/"downloads").string()&&directory.saveFile=="file.bin"&&!directory.legacySavePath,"existing directory gains metadata filename");
    for(const auto& path:{f.root/"actual.bin",f.root/"not-created.bin"}){const auto saved=take(storage::resolveOriginalSaveRoot(f.saved(path),layout));check(saved.saveDirectory==f.root.string()&&saved.saveFile==path.filename().string(),"file or missing simple path is complete filename");}
    check(!fs::exists(f.root/"not-created.bin"),"resolution never creates data");
    const auto explicitName=take(storage::resolveOriginalSaveRoot(f.saved(f.root/"downloads/",std::string("renamed.bin")),layout));check(explicitName.saveDirectory==(f.root/"downloads").string()&&explicitName.saveFile=="renamed.bin","explicit saved filename wins with trailing separator removed");
  });
  run("multifile-root-and-captured-existence-decision",[]{Fixture f;const auto layout=f.layout(true);fs::create_directory(f.root/"bundle");
    auto existing=take(storage::resolveOriginalSaveRoot(f.saved(f.root/"bundle"),layout));check(existing.saveDirectory==f.root.string()&&existing.saveFile=="bundle","selected existing metadata root is not nested twice");
    const auto path=f.root/"missing"/"bundle";const auto before=f.saved(path);const auto missing=take(storage::resolveOriginalSaveRoot(before,layout));check(fs::path(missing.saveDirectory)/ *missing.saveFile==path/"bundle","missing multifile path is parent even if basename equals name");
    fs::create_directories(path);const auto restored=take(storage::restoreOriginalSaveRoot(before,layout,path/"bundle"));check(restored.saveDirectory==path.string()&&restored.saveFile=="bundle","recovery retains approved pre-creation interpretation");
    check(!storage::restoreOriginalSaveRoot(before,layout,f.root/"foreign"),"recovery cannot authorize arbitrary root");write(f.root/"not-directory","x");check(!storage::resolveOriginalSaveRoot(f.saved(f.root/"not-directory"),layout),"multifile file root rejected");
  });
  run("unsafe-root-components-and-symlinks",[]{Fixture f;const auto layout=f.layout();write(f.root/"real/file","x");fs::create_directory_symlink(f.root/"real",f.root/"linked");
    for(const auto& path:{f.root/"linked/file",f.root/"real/../file",fs::path("relative/file")}){check(!storage::resolveOriginalSaveRoot(f.saved(path),layout),"unsafe root denied");}
    auto saved=f.saved(f.root,std::string("../escape"));check(!storage::resolveOriginalSaveRoot(saved,layout),"explicit name cannot be path");
  });
  run("missing-empty-types-use-original-default-linear",[]{Fixture f;const auto layout=f.layout();for(const auto& raw:{"de","d10:storetypeslee"}){auto attrs=doc(raw);const auto old=attrs.serialize();const auto result=f.types(layout,attrs);check(result.synthesized&&result.requested==std::vector{Kind::Linear}&&result.actual==result.requested&&!result.reorderMinimumMiB,"original absent defaults are L");check(attrs.serialize()==old&&!fs::exists(f.profile/"active"),"read projection creates no directories or state");}
  });
  run("configured-reorder-floor-threshold-and-captured-local",[]{Fixture f;auto layout=f.layout(true);layout.files={{"under",0,10*1024*1024-1,false},{"at",10*1024*1024-1,10*1024*1024,false}};layout.bytes=20*1024*1024-1;
    auto config=doc("de");put(config,"Enable reorder storage mode","i1e");write(f.profile/"biglybt.config",config.serialize());auto attrs=doc("de");auto result=f.types(layout,attrs);check(result.requested==std::vector{Kind::Linear,Kind::Reordered}&&result.reorderMinimumMiB==10,"original default 10MiB threshold floors file size");
    put(attrs,"reordermb","i11e");put(config,"Reorder storage mode min MB","i0e");write(f.profile/"biglybt.config",config.serialize());result=f.types(layout,attrs);check(result.requested==std::vector{Kind::Linear,Kind::Linear}&&!result.reorderMinimumMiB,"saved nonnegative threshold wins over global");
    put(attrs,"reordermb","i-1e");result=f.types(layout,attrs);check(result.requested==std::vector{Kind::Reordered,Kind::Reordered}&&result.reorderMinimumMiB==0,"negative per-state threshold captures source global");
  });
  run("bad-settings-cannot-silently-default-to-linear",[]{Fixture f;const auto layout=f.layout();auto attrs=doc("de");write(f.profile/"biglybt.config.saving","broken");check(!storage::resolveOriginalStorageTypes(f.lease,f.hash,layout,attrs.root()),"present unreadable settings rejected");fs::remove(f.profile/"biglybt.config.saving");auto config=doc("de");put(config,"Enable reorder storage mode",b("true"));write(f.profile/"biglybt.config",config.serialize());check(!storage::resolveOriginalStorageTypes(f.lease,f.hash,layout,attrs.root()),"wrong setting type rejected");
  });
  run("explicit-stores-do-not-load-unused-default-settings",[]{Fixture f;write(f.profile/"biglybt.config","broken");auto attrs=doc("d10:storetypesl1:Ree");auto result=f.types(f.layout(),attrs);check(!result.synthesized&&result.actual==std::vector{Kind::Reordered},"explicit storage policy independent of missing global defaults");for(const auto& raw:{"d10:storetypesi1ee","d10:storetypesl1:Qee","d10:storetypesl1:L1:Lee"}){auto bad=doc(raw);check(!storage::resolveOriginalStorageTypes(f.lease,f.hash,f.layout(),bad.root()),"wrong count/type/unknown physical token refused");}
  });
  run("compact-control-prevents-packed-linear-interpretation",[]{Fixture f;auto attrs=doc("de");const auto path=f.control(0,Kind::Compact);write(path,"d7:versioni0e6:lengthi32768ee");const auto bytes=fs::file_size(path);const auto result=f.types(f.layout(),attrs);check(result.requested==std::vector{Kind::Linear}&&result.actual==std::vector{Kind::Compact}&&!result.unsupportedConversion&&result.markers.size()==1,"C marker requires actual C and existing C-to-L path");check(fs::file_size(path)==bytes&&attrs.serialize()=="de","marker projection never edits source");fs::rename(path,fs::path(path.string()+".bak"));check(f.types(f.layout(),attrs).actual==std::vector{Kind::Compact},"retained C backup also prevents linear reinterpretation");
  });
  run("reordered-control-and-unsupported-cross-family",[]{Fixture f;auto attrs=doc("de");const auto path=f.control(0,Kind::Reordered);write(path,"d3:leni0e4:nexti1e6:pieces0:e");auto result=f.types(f.layout(),attrs);check(result.actual==std::vector{Kind::ReorderedCompact}&&result.unsupportedConversion,"default L cannot reinterpret existing reordered bytes");put(attrs,"storetypes","l1:Re");result=f.types(f.layout(),attrs);check(result.actual==std::vector{Kind::Reordered}&&!result.unsupportedConversion,"explicit R matches R marker");write(f.control(0,Kind::Compact),"de");check(!storage::resolveOriginalStorageTypes(f.lease,f.hash,f.layout(),attrs.root()),"ambiguous simultaneous control families refused");
  });
  run("unsafe-control-candidates-and-pending-publication",[]{Fixture f;auto attrs=doc("de");const auto path=f.control(0,Kind::Compact);write(path,"de");fs::create_hard_link(path,path.string()+".bak");check(!storage::resolveOriginalStorageTypes(f.lease,f.hash,f.layout(),attrs.root()),"hardlinked control refused");fs::remove(path.string()+".bak");fs::remove(path);fs::create_symlink(f.root/"foreign",path);check(!storage::resolveOriginalStorageTypes(f.lease,f.hash,f.layout(),attrs.root()),"symlink control refused");fs::remove(path);write(path.string()+".saving","de");check(!storage::resolveOriginalStorageTypes(f.lease,f.hash,f.layout(),attrs.root()),"saving candidate requires recovery");
  });
  run("default-materialization-preserves-opaque-values",[]{Fixture f;auto attrs=doc("d6:opaqueli-7e3:rawee");const auto opaque=core::encodeValue(*attrs.find("opaque")).value();const auto result=f.types(f.layout(),attrs);check(storage::applyOriginalStorageDefaults(attrs,result).has_value(),"materialize default");check(core::encodeValue(*attrs.find("opaque")).value()==opaque&&attrs.find("storetypes")->children[0].bytes=="L","unknown subtree unchanged");
  });
  run("marker-identity-is-part-of-revalidation",[]{Fixture f;auto attrs=doc("de");const auto path=f.control(0,Kind::Compact);write(path,"de");const auto first=f.types(f.layout(),attrs);fs::rename(path,path.string()+".old");write(path,"de");const auto second=f.types(f.layout(),attrs);check(first!=second&&first.actual==second.actual,"same-byte replacement changes captured marker identity");
  });
  std::cout<<"PASS "<<cases<<" original-storage-default cases; "<<assertions<<" assertions; generated paths only; no Java equality credit\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
