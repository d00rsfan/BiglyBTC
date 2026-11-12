// SPDX-License-Identifier: GPL-2.0-or-later
#include "torrent_options.hpp"
#include "torrent_state.hpp"
#include <algorithm>
#include <limits>
namespace biglybtc::storage {
namespace {
using core::Node;
auto fail(std::string message){return std::unexpected(Error{ErrorCode::Conflict,std::move(message)});}
std::expected<int,Error> number(const Node* node,int fallback=0){if(!node){return fallback;}if(node->type!=Node::Type::Integer||node->integer<0||node->integer>std::numeric_limits<int>::max()){return fail("Invalid original torrent option");}return int(node->integer);}
std::expected<core::Document,Error> dictionary(const Node* node){if(!node){return *core::Document::parse("de");}auto bytes=core::encodeValue(*node,compatibleConfigLimits());if(!bytes){return fail("Cannot encode original options");}auto doc=core::Document::parse(*bytes,compatibleConfigLimits());if(!doc||doc->root().type!=Node::Type::Dictionary){return fail("Original options are not a dictionary");}return std::move(*doc);}
const core::SavedDownload* target(const core::LibrarySnapshot& library,const core::TorrentHash& hash){const core::SavedDownload* selected=nullptr;for(const auto& row:library.downloads){if(row.hash&&*row.hash==hash){if(selected){return nullptr;}selected=&row;}}return selected;}
const Node& entry(const ConfigFile& file,const core::LibrarySnapshot& library,const core::SavedDownload& row){return library.legacyMap?file.document().root().children.at(row.sourceIndex*2+1):file.document().find("downloads")->children.at(row.sourceIndex);}
std::string integer(int value){return "i"+std::to_string(value)+"e";}
}
std::expected<OriginalTorrentOptions,Error> readOriginalTorrentOptions(const ProfileLease& lease,const core::TorrentHash& hash){
 auto config=ConfigFile::load(lease,"downloads.config");if(!config){return std::unexpected(config.error());}auto library=core::readLibrary(config->document());if(!library){return fail("Invalid original library");}const auto* row=target(*library,hash);if(!row){return fail("Ambiguous or absent torrent options identity");}
 OriginalTorrentOptions result;result.stopped=row->startup==core::StartupState::Stopped;const auto& saved=entry(*config,*library,*row);
 auto down=number(saved.find("maxdl")),up=number(saved.find("maxul"));if(!down||!up){return fail("Invalid original rate limits");}result.values[0]=*down;result.values[1]=*up;
 auto store=TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}auto state=store->loadAttributes(lease,hash);if(!state){return std::unexpected(state.error());}
 const auto* attrs=state->document().find("attributes");const auto* params=attrs?attrs->find("parameters"):nullptr;
 if(params&&params->type!=Node::Type::Dictionary){return fail("Invalid original torrent parameters");}
 auto peers=number(params?params->find("max.peers"):nullptr),slots=number(params?params->find("max.uploads"):nullptr,4);if(!peers||!slots){return fail("Invalid original connection limits");}result.values[2]=*peers;result.values[3]=*slots;return result;
}
std::expected<void,Error> saveOriginalTorrentOptions(const ProfileLease& lease,const core::TorrentHash& hash,bool rates,std::array<int,2> expected,std::array<int,2> desired){
 if(desired[0]<0||desired[1]<0||(!rates&&(desired[0]>65535||desired[1]>65535||desired[1]<2))){return fail("Unsupported torrent option value; upload slots must be at least two");}
 auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
 auto current=readOriginalTorrentOptions(lease,hash);if(!current){return std::unexpected(current.error());}if(!current->stopped){return fail("Stop the torrent before changing these options");}
 const int offset=rates?0:2;if(current->values[std::size_t(offset)]!=expected[0]||current->values[std::size_t(offset+1)]!=expected[1]){return fail("Torrent options changed since presentation; reload before applying");}
 if(expected==desired){return {};}
 if(rates){
  auto file=ConfigFile::load(lease,"downloads.config");if(!file){return std::unexpected(file.error());}auto library=core::readLibrary(file->document());if(!library){return fail("Invalid original library");}const auto* selected=target(*library,hash);if(!selected){return fail("Torrent no longer exists");}
  auto row=dictionary(&entry(*file,*library,*selected));if(!row||!row->replaceValue("maxdl",integer(desired[0]))||!row->replaceValue("maxul",integer(desired[1]))){return fail("Cannot update original rate limits");}
  if(library->legacyMap){if(!selected->legacyKey||!file->document().replaceValue(*selected->legacyKey,row->serialize())){return fail("Cannot update legacy torrent options");}}
  else{std::string list="l";const auto& rows=file->document().find("downloads")->children;for(std::size_t i=0;i<rows.size();++i){if(i==selected->sourceIndex){list+=row->serialize();}else{auto bytes=core::encodeValue(rows[i],compatibleConfigLimits());if(!bytes){return fail("Cannot preserve original library entry");}list+=*bytes;}}list+='e';if(!file->document().replaceValue("downloads",std::move(list))){return fail("Cannot update original library");}}
  return file->save(lease);
 }
 auto store=TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}auto state=store->loadAttributes(lease,hash);if(!state){return std::unexpected(state.error());}auto attrs=dictionary(state->document().find("attributes"));if(!attrs){return std::unexpected(attrs.error());}auto params=dictionary(attrs->find("parameters"));if(!params||!params->replaceValue("max.peers",integer(desired[0]))||!params->replaceValue("max.uploads",integer(desired[1]))||!attrs->replaceValue("parameters",params->serialize())){return fail("Cannot update original connection limits");}return store->saveAttributes(lease,*state,attrs->serialize());
}
}
