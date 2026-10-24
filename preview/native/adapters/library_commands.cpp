#include "library_commands.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace biglybtc::storage {
namespace {
using Action = core::DownloadAction;
using State = core::StartupState;
using Node = core::Node;
using Snapshot = core::LibrarySnapshot;
std::unexpected<Error> failure(ErrorCode code, const char* message) { return std::unexpected(Error{code,message}); }
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ":" + std::string(value); }
std::string integer(std::int64_t value) { return "i" + std::to_string(value) + "e"; }
std::string hashBytes(const core::TorrentHash& hash) {
  const auto value = hash.bytes(); return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::string raw(const core::Document& document, const Node& node) {
  return document.originalBytes().substr(node.begin,node.end-node.begin);
}
bool samePause(const std::optional<core::SavedPause>& a, const std::optional<core::SavedPause>& b) {
  return a.has_value() == b.has_value() && (!a || (a->hash == b->hash && a->forceStart == b->forceStart && a->owner == b->owner));
}
bool moveAction(Action action) {
  return action == Action::MoveTop || action == Action::MoveUp || action == Action::MoveDown || action == Action::MoveBottom;
}
std::expected<Snapshot,Error> inventory(const core::Document& document) {
  auto result = core::readLibrary(document);
  if (!result) { return failure(ErrorCode::InvalidFormat,"Invalid saved-download inventory"); }
  for (const auto& issue : result->issues) {
    if (issue.pauseEntry || !result->legacyMap) { return failure(ErrorCode::InvalidFormat,"Malformed saved download or pause entry"); }
    // Legacy roots also contain metadata. Leave opaque metadata untouched, but
    // never skip a malformed entry that advertises any download identity/path.
    const auto& node = document.root().children.at(issue.sourceIndex*2+1);
    const auto key = document.root().children.at(issue.sourceIndex*2).bytes;
    if (key != "pause_data" && (node.find("torrent") || node.find("torrent_hash")
        || node.find("save_dir") || node.find("path"))) {
      return failure(ErrorCode::InvalidFormat,"Malformed legacy saved download");
    }
  }
  std::unordered_set<std::string> hashes;
  for (const auto& item : result->downloads) {
    if (item.hash && !hashes.insert(item.hash->hex()).second) { return failure(ErrorCode::Conflict,"Duplicate saved-download identity"); }
  }
  hashes.clear();
  for (const auto& item : result->paused) {
    if (!hashes.insert(item.hash.hex()).second) { return failure(ErrorCode::Conflict,"Duplicate saved-pause identity"); }
  }
  return std::move(*result);
}
const Node& downloadNode(const core::Document& document, const core::SavedDownload& download, bool legacy) {
  return legacy ? document.root().children.at(download.sourceIndex*2+1)
      : document.find("downloads")->children.at(download.sourceIndex);
}
std::optional<core::SavedPause> pauseFor(const Snapshot& inventory, const core::TorrentHash& hash) {
  const auto found = std::find_if(inventory.paused.begin(),inventory.paused.end(),[&](const auto& pause) { return pause.hash == hash; });
  return found == inventory.paused.end() ? std::optional<core::SavedPause>{} : *found;
}
std::expected<void,Error> replace(core::Document& document, std::string_view key, std::string value) {
  if (!document.replaceValue(key,std::move(value))) { return failure(ErrorCode::InvalidFormat,"Saved command exceeds structured-edit limits"); }
  return {};
}
std::expected<std::string,Error> editDownload(const core::Document& original, const core::SavedDownload& before,
    const core::SavedDownload& after, bool legacy) {
  auto row = core::Document::parse(raw(original,downloadNode(original,before,legacy)),compatibleConfigLimits());
  if (!row) { return failure(ErrorCode::InvalidFormat,"Invalid saved-download edit source"); }
  if (before.originalState != after.originalState || before.startup != after.startup) {
    if (auto changed = replace(*row,"state",integer(after.originalState)); !changed) { return std::unexpected(changed.error()); }
    if (row->find("stopped")) {
      if (auto changed = replace(*row,"stopped",integer(after.startup == State::Stopped ? 1 : 0)); !changed) { return std::unexpected(changed.error()); }
    }
  }
  if (before.forceStart != after.forceStart) {
    if (auto changed = replace(*row,"forceStart",integer(after.forceStart ? 1 : 0)); !changed) { return std::unexpected(changed.error()); }
    if (row->find("startStopLocked")) {
      if (auto changed = replace(*row,"startStopLocked",integer(after.forceStart ? 1 : 0)); !changed) { return std::unexpected(changed.error()); }
    }
  }
  if (before.position != after.position) {
    if (!after.position) { return failure(ErrorCode::InvalidFormat,"Missing command position"); }
    if (auto changed = replace(*row,"position",integer(*after.position)); !changed) { return std::unexpected(changed.error()); }
  }
  return row->serialize();
}
void state(core::SavedDownload& value, State next) { value.startup = next; value.originalState = static_cast<std::int64_t>(next); }
void force(core::SavedDownload& value) {
  // Controller changes the state only when force_start actually changes.
  if (!value.forceStart) {
    value.forceStart = true;
    if (value.startup == State::Stopped || value.startup == State::Queued) { state(value,State::Waiting); }
  }
}
std::expected<void,Error> reorder(const Snapshot& saved, const core::TorrentHash& target, Action action,
    std::span<const LibraryCompletionPool> pools, LibraryCommandResult& result,
    std::unordered_map<std::size_t,std::int64_t>& edits) {
  if (pools.size() != saved.downloads.size() || pools.empty()) {
    return failure(ErrorCode::Unsupported,"Moves require qualified completion pools for every saved download");
  }
  std::unordered_map<std::string,bool> completion;
  for (const auto& item : pools) {
    if (!completion.emplace(item.hash.hex(),item.complete).second) { return failure(ErrorCode::Conflict,"Duplicate completion-pool identity"); }
  }
  std::array<std::size_t,2> counts{};
  for (const auto& item : saved.downloads) {
    if (!item.hash || !completion.contains(item.hash->hex())) { return failure(ErrorCode::Unsupported,"Incomplete saved-download completion classification"); }
    ++counts[completion.at(item.hash->hex()) ? 1U : 0U];
  }
  std::array<std::unordered_set<std::int64_t>,2> positions;
  for (const auto& item : saved.downloads) {
    const auto group = completion.at(item.hash->hex()) ? 1U : 0U;
    if (!item.position || *item.position < 1 || static_cast<std::uint64_t>(*item.position) > counts[group]
        || !positions[group].insert(*item.position).second) {
      return failure(ErrorCode::Unsupported,"Saved positions require startup normalization before moving");
    }
  }
  const bool group = completion.at(target.hex()); const auto old = *result.before.position;
  const auto limit = static_cast<std::int64_t>(counts[group ? 1U : 0U]);
  const auto next = action == Action::MoveTop ? 1 : action == Action::MoveBottom ? limit
      : action == Action::MoveUp ? std::max<std::int64_t>(1,old-1) : std::min(limit,old+1);
  if (next == old) { return {}; }
  for (const auto& item : saved.downloads) {
    if (completion.at(item.hash->hex()) != group) { continue; }
    auto position = *item.position;
    if (*item.hash == target) { position = next; }
    else if (next < old && position >= next && position < old) { ++position; }
    else if (next > old && position > old && position <= next) { --position; }
    if (position != *item.position) {
      result.positions.push_back({*item.hash,*item.position,position}); edits.emplace(item.sourceIndex,position);
    }
  }
  result.after.position = next;
  return {};
}
} // namespace

std::expected<LibraryCommandResult,Error> applyLibraryCommand(ProfileLease& lease,
    const core::TorrentHash& hash, Action action, std::span<const LibraryCompletionPool> pools) {
  bool saving = false;
  try {
    if (static_cast<unsigned>(action) > static_cast<unsigned>(Action::MoveBottom)) {
      return failure(ErrorCode::Unsupported,"Unknown saved-download action");
    }
    auto file = ConfigFile::load(lease,"downloads.config");
    if (!file) { return std::unexpected(file.error()); }
    if (file->recoverySource() == RecoverySource::Saving) { return failure(ErrorCode::Conflict,"Resolve the existing recovery file before a library command"); }
    auto saved = inventory(file->document());
    if (!saved) { return std::unexpected(saved.error()); }
    const auto found = std::find_if(saved->downloads.begin(),saved->downloads.end(),[&](const auto& item) { return item.hash && *item.hash == hash; });
    if (found == saved->downloads.end()) { return failure(ErrorCode::Conflict,"Saved-download target is stale or unavailable"); }
    LibraryCommandResult result{action,*found,*found,pauseFor(*saved,hash),pauseFor(*saved,hash),{},false,file->recoverySource()};
    std::unordered_map<std::size_t,std::int64_t> positions;
    const auto beforeState = result.before.startup;
    if (moveAction(action)) {
      if (auto moved = reorder(*saved,hash,action,pools,result,positions); !moved) { return std::unexpected(moved.error()); }
    } else if (action == Action::Pause) {
      if (!result.pauseBefore) {
        result.pauseAfter = core::SavedPause{hash,result.before.forceStart,1};
        if (beforeState != State::Stopped) { state(result.after,State::Stopped); result.after.forceStart = false; }
      }
    } else if (action == Action::Resume) {
      if (result.pauseBefore) {
        result.pauseAfter.reset();
        if (beforeState == State::Stopped) {
          if (result.pauseBefore->forceStart) { force(result.after); }
          else { state(result.after,State::Queued); }
        }
      }
    } else if (action == Action::Start || action == Action::Queue) {
      if (beforeState == State::Stopped) {
        result.pauseAfter.reset();
        if (result.pauseBefore && result.pauseBefore->forceStart) { force(result.after); }
        else { state(result.after,action == Action::Start ? State::Waiting : State::Queued); }
      }
    } else if (action == Action::Stop) {
      result.pauseAfter.reset();
      // ManagerUtils.asyncStop dispatches stopPausedDownload instead of stopIt
      // when paused; do not invent an additional controller transition.
      if (!result.pauseBefore && beforeState != State::Stopped) { state(result.after,State::Stopped); result.after.forceStart = false; }
    } else if (action == Action::ForceStart) {
      result.pauseAfter.reset(); force(result.after);
    }
    const bool rowChanged = result.before.originalState != result.after.originalState
        || result.before.startup != result.after.startup || result.before.forceStart != result.after.forceStart
        || result.before.position != result.after.position;
    const bool pauseChanged = !samePause(result.pauseBefore,result.pauseAfter);
    result.changed = rowChanged || pauseChanged || !positions.empty();
    if (!result.changed) { return result; }
    // Build every replacement from pristine source slices before changing the
    // root tree. All unselected rows, values, key order and opaque tails survive.
    std::unordered_map<std::size_t,std::string> rows;
    for (const auto& item : saved->downloads) {
      auto after = item;
      if (item.hash && *item.hash == hash) { after = result.after; }
      if (const auto pos = positions.find(item.sourceIndex); pos != positions.end()) { after.position = pos->second; }
      if (!(item.hash && *item.hash == hash && rowChanged) && !positions.contains(item.sourceIndex)) { continue; }
      auto encoded = editDownload(file->document(),item,after,saved->legacyMap);
      if (!encoded) { return std::unexpected(encoded.error()); }
      rows.emplace(item.sourceIndex,std::move(*encoded));
    }
    std::vector<std::pair<std::string,std::string>> replacements;
    if (saved->legacyMap) {
      for (const auto& item : saved->downloads) {
        if (const auto row = rows.find(item.sourceIndex); row != rows.end()) { replacements.emplace_back(*item.legacyKey,row->second); }
      }
    } else if (!rows.empty()) {
      std::string encoded = "l"; const auto& entries = file->document().find("downloads")->children;
      for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto row = rows.find(i); encoded += row == rows.end() ? raw(file->document(),entries[i]) : row->second;
      }
      encoded += 'e'; replacements.emplace_back("downloads",std::move(encoded));
    }
    if (pauseChanged) {
      std::string encoded = "l";
      if (const auto* pauses = file->document().find("pause_data")) {
        for (const auto& pause : pauses->children) {
          const auto* key = pause.type == Node::Type::Bytes ? &pause : pause.find("hash");
          if (key->bytes != hashBytes(hash)) { encoded += raw(file->document(),pause); }
        }
      }
      if (result.pauseAfter) {
        encoded += "d4:hash" + bytes(hashBytes(hash)) + "5:force" + integer(result.pauseAfter->forceStart ? 1 : 0)
            + "5:owner" + integer(result.pauseAfter->owner) + "e";
      }
      encoded += 'e'; replacements.emplace_back("pause_data",std::move(encoded));
    }
    for (auto& [key,value] : replacements) {
      if (auto changed = replace(file->document(),key,std::move(value)); !changed) { return std::unexpected(changed.error()); }
    }
    auto parsed = core::Document::parse(file->document().serialize(),compatibleConfigLimits());
    if (!parsed) { return failure(ErrorCode::InvalidFormat,"Invalid edited saved-download document"); }
    auto checked = inventory(*parsed);
    if (!checked || checked->downloads.size() != saved->downloads.size()) { return failure(ErrorCode::InvalidFormat,"Edited saved-download inventory failed validation"); }
    const auto target = std::find_if(checked->downloads.begin(),checked->downloads.end(),[&](const auto& item) { return item.hash && *item.hash == hash; });
    if (target == checked->downloads.end() || target->originalState != result.after.originalState
        || target->startup != result.after.startup || target->forceStart != result.after.forceStart || target->position != result.after.position
        || !samePause(pauseFor(*checked,hash),result.pauseAfter)) {
      return failure(ErrorCode::InvalidFormat,"Edited saved-download intent failed validation");
    }
    if (!result.positions.empty()) {
      std::unordered_map<std::string,std::optional<std::int64_t>> confirmed;
      for (const auto& item : checked->downloads) { if (item.hash) { confirmed.emplace(item.hash->hex(),item.position); } }
      for (const auto& change : result.positions) {
        const auto item = confirmed.find(change.hash.hex());
        if (item == confirmed.end() || item->second != change.after) { return failure(ErrorCode::InvalidFormat,"Edited saved positions failed validation"); }
      }
    }
    // Stage the whole returned intent before publication; success performs only
    // moves afterward. ConfigFile retains its actual CAS/recovery/error policy.
    std::expected<LibraryCommandResult,Error> prepared(std::move(result));
    saving = true;
    auto committed = file->save(lease);
    if (!committed) { return std::unexpected(committed.error()); }
    return prepared;
  } catch (const std::bad_alloc&) {
    return failure(ErrorCode::Io,saving ? "Library command allocation failed; configuration may have changed"
        : "Insufficient memory preparing saved-library command");
  }
}
} // namespace biglybtc::storage
