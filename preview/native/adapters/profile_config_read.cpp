// SPDX-License-Identifier: GPL-2.0-or-later
#include "parsed_config_cache.hpp"
#include "profile_config_native.hpp"

#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}
} // namespace

ConfigReadCacheStatistics ProfileLease::configReadCacheStatistics() const {
  if(!ownership_){return {};}
  const std::lock_guard lock(ownership_->mutex);
  if(!ownership_->parsedConfigs){return {};}
  auto out=ownership_->parsedConfigs->statistics;out.entries=ownership_->parsedConfigs->entries.size();return out;
}

std::expected<ConfigFile, Error> ConfigFile::load(const ProfileLease& lease, std::string filename, core::Limits limits) {
  if (!lease.valid() || !detail::validConfigName(filename)) {
    return error(ErrorCode::UnsafePath, "Held profile lease and safe configuration filename required");
  }
  auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
  std::shared_ptr<ParsedConfigCache> cache;
  try{if(!lease.ownership_->parsedConfigs){lease.ownership_->parsedConfigs=std::make_shared<ParsedConfigCache>();}cache=lease.ownership_->parsedConfigs;}catch(const std::bad_alloc&){/* Optional acceleration only. */}
  if(cache){ParsedConfigCache::increment(cache->statistics.loads);}
  const auto directory = detail::profileConfigDirectory(lease);
  if (!directory) { return std::unexpected(directory.error()); }
  auto primary = detail::readProfileConfig(lease, filename, limits.inputBytes);
  if (!primary) {
    return std::unexpected(primary.error());
  }
  const bool missing = !primary->has_value();
  if (!missing) {
    if(cache){if(auto hit=cache->find<core::Document>(filename,limits,**primary)){
      return ConfigFile(std::move(filename),std::move(*hit),RecoverySource::Primary,
          std::nullopt,false,directory->volume,directory->file,limits);
    }ParsedConfigCache::increment(cache->statistics.parses);}
    auto parsed = core::Document::parse(**primary, limits);
    if (parsed && parsed->root().type == core::Node::Type::Dictionary) {
      if(cache){try{cache->remember(filename,limits,*parsed);}catch(const std::bad_alloc&){/* Cache exhaustion cannot refuse a valid configuration. */}}
      return ConfigFile(std::move(filename), std::move(*parsed), RecoverySource::Primary,
          std::nullopt, false, directory->volume, directory->file, limits);
    }
  }
  const std::string candidates[] = {missing ? ".saving" : ".bak", missing ? ".bak" : ".saving"};
  for (const auto& suffix : candidates) {
    auto candidate = detail::readProfileConfig(lease, filename + suffix, limits.inputBytes);
    if (!candidate) {
      return std::unexpected(candidate.error());
    }
    if (!candidate->has_value()) {
      continue;
    }
    auto parsed = core::Document::parse(std::move(**candidate), limits);
    if (parsed && parsed->root().type == core::Node::Type::Dictionary) {
      return ConfigFile(std::move(filename), std::move(*parsed),
          suffix == ".bak" ? RecoverySource::Backup : RecoverySource::Saving,
          std::move(*primary), missing, directory->volume, directory->file, limits);
    }
  }
  return error(missing ? ErrorCode::Missing : ErrorCode::InvalidFormat, "No complete compatible configuration could be read");
}

std::expected<void, Error> ConfigFile::save(const ProfileLease& lease, void (*observer)(SaveStage)) {
  return saveImpl(lease, observer, std::nullopt);
}

std::expected<void, Error> ConfigFile::saveReplacement(const ProfileLease& lease, std::string bytes,
    const std::function<std::expected<void, Error>()>& publishStates) {
  auto parsed = core::Document::parse(std::move(bytes), limits_);
  if (!parsed || parsed->root().type != core::Node::Type::Dictionary) {
    return error(ErrorCode::InvalidFormat, "Invalid replacement configuration");
  }
  const auto states = publishStates();
  if (!states) { return std::unexpected(states.error()); }
  return saveImpl(lease, nullptr, std::move(*parsed));
}

void ConfigFile::recordSuccessfulSave(const ProfileLease& lease, core::Document document,
                                      bool rememberReplacement) {
  document_ = std::move(document);
  if (rememberReplacement && lease.ownership_->parsedConfigs) {
    try {
      lease.ownership_->parsedConfigs->remember(filename_, limits_, document_);
    } catch (const std::bad_alloc&) {
      // The existing file and directory fences have already completed.
    }
  }
  source_ = RecoverySource::Primary;
  damagedPrimary_.reset();
  primaryMissing_ = false;
}

} // namespace biglybtc::storage
