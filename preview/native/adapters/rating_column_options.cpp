// SPDX-License-Identifier: GPL-2.0-only
// Original RatingSWTUI preference, azrating, Aelitis SARL.
#include "rating_column_options.hpp"
#include <unordered_set>
namespace biglybtc::storage {
namespace {
std::expected<void,Error> valid(const ConfigFile& file){
 if(file.recoverySource()!=RecoverySource::Primary){return std::unexpected(Error{ErrorCode::Conflict,"Resolve rating sort configuration recovery first"});}const auto& root=file.document().root();if(root.type!=core::Node::Type::Dictionary||root.children.size()%2){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid rating sort configuration"});}std::unordered_set<std::string_view> keys;for(std::size_t n=0;n<root.children.size();n+=2){if(!keys.insert(root.children[n].bytes).second){return std::unexpected(Error{ErrorCode::InvalidFormat,"Ambiguous rating sort configuration"});}}return {};
}
}
std::expected<bool,Error> readRatingPersonalSort(const ProfileLease& lease){
 auto file=ConfigFile::load(lease,"biglybt.config");if(!file){if(file.error().code==ErrorCode::Missing){return false;}return std::unexpected(file.error());}auto checked=valid(*file);if(!checked){return std::unexpected(checked.error());}const auto* field=file->document().find("ratingplugin.col.sort");return field&&field->type==core::Node::Type::Integer&&static_cast<std::uint32_t>(field->integer)!=0;
}
std::expected<bool,Error> saveRatingPersonalSort(const ProfileLease& lease,bool personal){
 auto file=ConfigFile::load(lease,"biglybt.config");if(!file&&file.error().code==ErrorCode::Missing){file=ConfigFile::createMissing(lease,"biglybt.config","de");}if(!file){return std::unexpected(file.error());}auto checked=valid(*file);if(!checked){return std::unexpected(checked.error());}const auto* field=file->document().find("ratingplugin.col.sort");if(field&&field->type==core::Node::Type::Integer&&field->integer==(personal?1:0)){return false;}auto replaced=file->document().replaceValue("ratingplugin.col.sort",personal?"i1e":"i0e");if(!replaced){return std::unexpected(Error{ErrorCode::InvalidFormat,"Cannot encode rating sort preference"});}auto saved=file->save(lease);if(!saved){return std::unexpected(saved.error());}return true;
}
}
