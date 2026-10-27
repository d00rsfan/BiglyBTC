#pragma once
#include "torrent_intake.hpp"
#include "allocation_intent.hpp"
#include "torrent_state.hpp"
#include <libtorrent/hasher.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::test::allocation {
namespace fs=std::filesystem;
inline void check(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
inline std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
inline std::string i(std::int64_t value){return "i"+std::to_string(value)+"e";}
inline void write(const fs::path& path,std::string_view value){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(value.data(),static_cast<std::streamsize>(value.size()));out.close();check(!out.fail(),"Generated allocation write");}
inline std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);check(input.good(),"Generated allocation read");return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
struct Fixture {
  fs::path root,profile,payload,metadata;
  bool multi,retain=false;
  std::vector<std::int64_t> sizes;
  core::TorrentHash hash;
  std::string info;
  std::optional<storage::ProfileLease> lease;
  explicit Fixture(bool multiple=false,std::optional<fs::path> exportRoot={}):multi(multiple){
    if(exportRoot){root=*exportRoot;check(root.is_absolute()&&root.lexically_normal()==root&&root.parent_path()=="/tmp"&&root.filename().string().starts_with("biglybtc-allocation-oracle-")&&root.filename().native().size()<=200,"Fresh flat owned allocation oracle root required");check(::mkdir(root.c_str(),0700)==0,"Allocation oracle root must be absent");retain=true;}
    else{char pattern[]="/tmp/biglybtc-allocation-XXXXXX";const auto* value=::mkdtemp(pattern);check(value,"Generated allocation root");root=value;}
    profile=root/"profile";payload=root/"payload";metadata=root/"source.torrent";fs::create_directories(profile/"active");fs::create_directory(payload);check(::chmod(profile.c_str(),0700)==0,"Private allocation profile");
    sizes=multi?std::vector<std::int64_t>{3*1024*1024+19,2*1024*1024+37}:std::vector<std::int64_t>{5*1024*1024+57};
    std::string body;for(std::size_t n=0;n<sizes.size();++n){body+=std::string(static_cast<std::size_t>(sizes[n]),static_cast<char>('A'+n));}
    std::string pieces;for(std::size_t at=0;at<body.size();at+=16384){const auto count=std::min<std::size_t>(16384,body.size()-at);pieces+=libtorrent::hasher(body.data()+at,static_cast<int>(count)).final().to_string();}
    info="d";if(multi){info+=b("files")+"l";for(std::size_t n=0;n<sizes.size();++n){info+="d6:length"+i(sizes[n])+"4:pathl"+b(n?"second.bin":"first.bin")+"ee";}info+='e';}else{info+="6:length"+i(sizes[0]);}
    info+=b("name")+b(multi?"bundle":"sample.bin")+b("piece length")+i(16384)+b("pieces")+b(pieces)+'e';
    const auto parsed=core::TorrentHash::parse(libtorrent::hasher(info.data(),static_cast<int>(info.size())).final().to_string());check(parsed.has_value(),"Generated allocation full hash");hash=*parsed;
    write(metadata,"d4:info"+info+"10:attributesd6:opaqueli7e3:rawee13:opaque.source3:rawe");
    write(profile/"downloads.config","d9:downloadsle6:opaqued3:keyli-3e3:raweee");
    auto held=storage::ProfileLease::acquire(profile);check(held.has_value(),"Generated allocation lease");lease.emplace(std::move(*held));
  }
  ~Fixture(){lease.reset();if(!retain){std::error_code error;fs::remove_all(root,error);}}
  Fixture(const Fixture&)=delete;
  fs::path path(std::size_t n=0)const{return multi?payload/"bundle"/(n?"second.bin":"first.bin"):payload/"sample.bin";}
  std::expected<core::TorrentHash,storage::Error> import(core::TorrentAllocationMode mode,void(*observer)(storage::TorrentIntakeStage)=nullptr){
    auto source=storage::inspectTorrentIntake(metadata);if(!source){return std::unexpected(source.error());}
    std::vector<compat::FilePolicy> policies;for(const auto& file:source->layout().files){policies.push_back({file.padding,0});}
    return storage::importTorrentWithFileOptions(*lease,*source,payload,true,policies,observer,false,{}, {},false,false,false,{}, {},false,{},core::ExistingVerificationGoal::FullTorrent,mode);
  }
  core::Document saved()const{auto value=storage::ConfigFile::load(*lease,"downloads.config");check(value.has_value(),"Allocation saved inventory");return value->document();}
  core::Document state()const{auto store=storage::TorrentStateStore::open(*lease);check(store.has_value(),"Allocation state store");auto value=store->load(*lease,hash);check(value.has_value(),"Allocation original state");return value->document();}
  const core::Node& row(const core::Document& value)const{const auto* rows=value.find("downloads");check(rows&&rows->children.size()==1,"Allocation row count");return rows->children[0];}
  std::int64_t bytes()const{std::int64_t result=0;for(auto value:sizes){result+=value;}return result;}
  bool zeroes()const{for(std::size_t n=0;n<sizes.size();++n){if(!fs::exists(path(n))||fs::file_size(path(n))!=static_cast<std::uintmax_t>(sizes[n])){return false;}const auto value=read(path(n));if(!std::ranges::all_of(value,[](char c){return c==0;})){return false;}}return true;}
};
} // namespace biglybtc::test::allocation
