// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"

#include <algorithm>
#include <variant>

namespace biglybtc::storage {

// Immutable parsed roots only. Every lookup follows a fresh existing readFile
// with its regular-file/no-follow/stability checks and compares ALL bytes.
// This cache never supplies a write permit, mtime certificate or CAS bypass.
struct ParsedConfigCache {
  static constexpr std::size_t MaximumEntries=8,MaximumBytes=64*1024*1024;
  struct Entry {std::string name;core::Limits limits;std::variant<core::Document,EncodedDocument> document;std::size_t bytes;};
  std::vector<Entry> entries;
  ConfigReadCacheStatistics statistics;
  static void increment(std::uint64_t& value)noexcept {if(value!=std::numeric_limits<std::uint64_t>::max()){++value;}}
  static bool same(const core::Limits& a,const core::Limits& b)noexcept {
    return a.inputBytes==b.inputBytes&&a.stringBytes==b.stringBytes&&a.nodes==b.nodes&&a.depth==b.depth;
  }
  template<class Document> std::optional<Document> find(std::string_view name,const core::Limits& limits,std::string_view bytes) {
    for(auto it=entries.begin();it!=entries.end();++it){
      if(it->name!=name||!same(it->limits,limits)){continue;}
      const auto* document=std::get_if<Document>(&it->document);if(!document||document->originalBytes()!=bytes){continue;}
      Document result=*document;increment(statistics.hits);std::rotate(it,it+1,entries.end());return result;
    }return {};
  }
  template<class Document> void remember(std::string_view name,core::Limits limits,const Document& document) {
    const auto payload=document.storageBytes();const auto overhead=sizeof(Entry)+name.size()+64;if(name.size()>512||overhead>MaximumBytes||payload>MaximumBytes-overhead){return;}const auto bytes=payload+overhead;
    for(auto it=entries.begin();it!=entries.end();){if(it->name==name&&same(it->limits,limits)&&std::holds_alternative<Document>(it->document)){statistics.retainedBytes-=it->bytes;it=entries.erase(it);}else{++it;}}
    while(!entries.empty()&&(entries.size()>=MaximumEntries||bytes>MaximumBytes-statistics.retainedBytes)){
      statistics.retainedBytes-=entries.front().bytes;entries.erase(entries.begin());increment(statistics.evictions);
    }
    entries.push_back({std::string(name),limits,document,bytes});statistics.retainedBytes+=bytes;statistics.entries=entries.size();
  }
};

} // namespace biglybtc::storage
