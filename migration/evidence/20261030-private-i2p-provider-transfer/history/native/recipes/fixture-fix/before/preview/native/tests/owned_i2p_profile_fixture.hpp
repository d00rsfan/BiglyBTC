// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile_source.hpp"
#include <QTemporaryDir>
#include <openssl/evp.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>
#include <stdexcept>
#include <sys/stat.h>
namespace owned_i2p_test {
namespace fs=std::filesystem;
namespace core=biglybtc::core;namespace storage=biglybtc::storage;
inline std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
inline std::string dict(std::initializer_list<std::pair<std::string,std::string>> entries){std::vector<std::pair<std::string,std::string>> sorted(entries);std::sort(sorted.begin(),sorted.end());std::string out="d";for(const auto& [key,value]:sorted){out+=b(key)+value;}return out+'e';}
inline void write(const fs::path& path,std::string_view bytes){std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!output){throw std::runtime_error("Owned I2P fixture write");}}
inline std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);if(!input){throw std::runtime_error("Owned I2P fixture read");}return {std::istreambuf_iterator<char>(input),{}};}
inline std::string sha1(std::string_view bytes){std::string result(20,'\0');unsigned n=0;if(EVP_Digest(bytes.data(),bytes.size(),reinterpret_cast<unsigned char*>(result.data()),&n,EVP_sha1(),nullptr)!=1||n!=20){throw std::runtime_error("Owned I2P fixture digest");}return result;}
struct Fixture {
  QTemporaryDir temporary;
  fs::path path=temporary.path().toStdString(),key=path/"plugins/azneti2phelper/dest_key1.dat";
  std::string payload="actual-owned-fixture-bytes",info= dict({{"length","i"+std::to_string(payload.size())+"e"},{"name",b("owned.bin")},{"piece length","i16384e"},{"pieces",b(sha1(payload))}});
  core::TorrentHash hash=*core::TorrentHash::parse(sha1(info));
  std::optional<storage::ProfileLease> lease;
  fs::path state()const{return path/"active"/(hash.hex()+".dat");}
  explicit Fixture(bool publicNetwork=false){
    if(!temporary.isValid()){throw std::runtime_error("Owned I2P temporary profile");}
    fs::create_directories(key.parent_path());fs::create_directory(path/"active");fs::create_directory(path/"payload");
    const auto fixture=fs::path(BIGLYBTC_I2P_TEST_KEYS_DIR);
    const auto destination=read(fixture/"ed25519.sender"),secret=read(fixture/"ed25519.known-test-key");
    write(key,destination+std::string(256,'e')+secret);if(::chmod(key.c_str(),0600)!=0){throw std::runtime_error("Owned fixture key mode");}
    const auto tracker=std::string("http://")+std::string(52,'a')+".b32.i2p:80/announce";
    write(path/"biglybt.config",dict({{"PluginInfo.azneti2phelper.enabled","i1e"},{"Plugin.azneti2phelper.enable","i1e"},{"Plugin.azneti2phelper.azi2phelper.pure.enabled","i1e"},{"future",b(std::string("x\0y",3))}}));
    write(state(),dict({{"announce",b(tracker)},{"attributes",dict({{"networks","l"+b(publicNetwork?"Public":"I2P")+"e"},{"peersources","l"+b("Tracker")+"e"}})},{"info",info}}));
    write(path/"fixture.torrent",dict({{"announce",b(tracker)},{"info",info}}));
    write(path/"downloads.config",dict({{"downloads","l"+dict({{"persistent","i1e"},{"save_dir",b((path/"payload").string())},{"save_file",b("owned.bin")},{"state","i75e"},{"torrent",b((path/"fixture.torrent").string())},{"torrent_hash",b(sha1(info))}})+"e"}}));
    auto held=storage::ProfileLease::acquire(path);if(!held){throw std::runtime_error(held.error().message);}lease.emplace(std::move(*held));
  }
};
}
