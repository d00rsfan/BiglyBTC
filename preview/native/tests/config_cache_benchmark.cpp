// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <sys/resource.h>
#include <unistd.h>
using namespace biglybtc;
namespace fs=std::filesystem;
std::string b(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
int main(){char pattern[]="/tmp/biglybtc-config-scale-XXXXXX";auto* created=::mkdtemp(pattern);if(!created){return 1;}const fs::path root(created);const auto cleanup=[&]{std::error_code error;fs::remove_all(root,error);};try{
 std::string rows;for(int n=0;n<2048;++n){rows+="d";for(int f=0;f<12;++f){rows+=b("field"+std::to_string(f))+"i"+std::to_string(n+f)+"e";}rows+='e';}const std::string raw="d9:downloadsl"+rows+"e6:opaque"+b(std::string(1024*1024,'p'))+'e';{std::ofstream out(root/"downloads.config",std::ios::binary);out.write(raw.data(),std::streamsize(raw.size()));}
 const auto start=std::chrono::steady_clock::now();std::size_t nodes=0;for(int n=0;n<128;++n){auto doc=core::Document::parse(raw,storage::compatibleConfigLimits());if(!doc){throw std::runtime_error("Decode control");}nodes+=doc->find("downloads")->children.size();}const auto decoded=std::chrono::steady_clock::now();
 auto lease=storage::ProfileLease::acquire(root);if(!lease){throw std::runtime_error("Lease");}std::vector<storage::ConfigFile> held;std::set<const core::Node*> roots;for(int n=0;n<128;++n){auto doc=storage::ConfigFile::load(*lease,"downloads.config");if(!doc){throw std::runtime_error("Cached read");}roots.insert(&doc->document().root());held.push_back(std::move(*doc));}const auto loaded=std::chrono::steady_clock::now();const auto stats=lease->configReadCacheStatistics();if(stats.parses!=1||stats.hits!=127||roots.size()!=1||nodes!=128*2048){throw std::runtime_error("Scale invariant");}
 std::cout<<"{\"status\":\"PASS_CONFIG_SCALE\",\"rows\":2048,\"loads\":128,\"input_bytes\":"<<raw.size()<<",\"decode_control_us\":"<<std::chrono::duration_cast<std::chrono::microseconds>(decoded-start).count()<<",\"fresh_cached_reads_us\":"<<std::chrono::duration_cast<std::chrono::microseconds>(loaded-decoded).count()<<",\"parses\":"<<stats.parses<<",\"hits\":"<<stats.hits<<",\"unique_retained_trees\":"<<roots.size()<<",\"retained_bytes\":"<<stats.retainedBytes<<"}\n";cleanup();return 0;
}catch(const std::exception& e){cleanup();std::cerr<<e.what()<<'\n';return 1;}}
