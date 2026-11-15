// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_strip_profile.hpp"
namespace biglybtc::app {
std::expected<ui::TagStripSettings,storage::Error> saveTagStripSettings(const storage::ProfileLease& lease,const ui::TagStripSettings& expected,const ui::TagStripSettings& value){
 using storage::Error;using storage::ErrorCode;
 if(value.alignment!=expected.alignment&&(value.alignment<0||value.alignment>3)){return std::unexpected(Error{ErrorCode::Unsupported,"Unknown library filter alignment"});}
 auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}auto config=storage::ConfigFile::load(lease,"biglybt.config");
 if(!config){if(config.error().code!=ErrorCode::Missing){return std::unexpected(config.error());}auto doc=core::Document::parse("de");auto changed=ui::editTagStripSettings(*doc,expected,value);if(!changed){return std::unexpected(Error{ErrorCode::Conflict,"Library filter settings changed"});}if(*changed){auto created=storage::ConfigFile::createMissing(lease,"biglybt.config",doc->serialize());if(!created){return std::unexpected(created.error());}}return value;}
 if(config->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(Error{ErrorCode::Conflict,"Resolve library settings recovery before saving"});}auto changed=ui::editTagStripSettings(config->document(),expected,value);if(!changed){return std::unexpected(Error{ErrorCode::Conflict,"Library filter settings changed"});}if(*changed){auto saved=config->save(lease);if(!saved){return std::unexpected(saved.error());}}return value;
}
}
