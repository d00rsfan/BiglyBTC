// SPDX-License-Identifier: GPL-2.0-or-later
#include "scheduler.hpp"
#include "biglybtc/scheduler.h"
#include <charconv>
#include <cstring>
#include <unordered_set>
namespace scheduler = biglybtc::plugins::speed_scheduler;
namespace {
struct Instance { bool running = false; };
bt_status create(const bt_plugin_options_v1* options, void** result) noexcept {
  if (!result) { return BT_INVALID; }
  *result = nullptr;
  if (!options || options->size != sizeof(*options) || options->abi != 1 || options->reserved) { return BT_INVALID; }
  try { *result = new Instance; return BT_OK; } catch (...) { return BT_FAILED; }
}
bt_status start(void* state) noexcept {
  if (!state) { return BT_INVALID; }
  static_cast<Instance*>(state)->running = true; return BT_OK;
}
bt_status evaluate(const bt_scheduler_request_v1& request, bt_scheduler_result_v1& response) {
  response.action_count = response.flags = response.reserved = 0;
  if (!request.xml || request.xml_size > 8 * 1024 * 1024 || request.xml_size == 0
      || request.sunday_based_day > 6 || request.minute > 1439 || (request.flags & ~15U)
      || request.reserved || request.torrent_count > scheduler::MaximumTorrents
      || (request.torrent_count && !request.torrents) || (response.action_capacity && !response.actions)) {
    return BT_INVALID;
  }
  auto xml = scheduler::readOriginalXml(std::string(reinterpret_cast<const char*>(request.xml), request.xml_size));
  if (!xml) { return BT_INVALID; }
  std::vector<scheduler::Torrent> torrents;
  std::unordered_set<std::uint64_t> unique;
  torrents.reserve(request.torrent_count);
  std::size_t totalMemberships = 0;
  for (std::uint32_t i = 0; i < request.torrent_count; ++i) {
    const auto& torrent = request.torrents[i];
    totalMemberships += torrent.membership_count;
    if (torrent.state < 1 || torrent.state > 9 || torrent.reserved || (torrent.flags & ~15U)
        || torrent.membership_count > 10000 || totalMemberships > 1000000
        || (torrent.membership_count && !torrent.memberships) || !unique.insert(torrent.token).second) { return BT_INVALID; }
    scheduler::Torrent value{.id = std::to_string(torrent.token),
        .complete = bool(torrent.flags & BT_TORRENT_COMPLETE), .state = static_cast<scheduler::TorrentState>(torrent.state),
        .forceStarted = bool(torrent.flags & BT_TORRENT_FORCED), .paused = bool(torrent.flags & BT_TORRENT_PAUSED),
        .ownedPause = bool(torrent.flags & BT_TORRENT_OWNED_PAUSE)};
    for (std::uint32_t j = 0; j < torrent.membership_count; ++j) {
      if (!torrent.memberships[j] || ::strnlen(torrent.memberships[j], 4097) > 4096) { return BT_INVALID; }
      value.memberships.emplace_back(torrent.memberships[j]);
    }
    torrents.push_back(std::move(value));
  }
  const auto tick = scheduler::evaluate(xml->configuration,
      {static_cast<int>(request.sunday_based_day), static_cast<int>(request.minute)}, torrents,
      {request.effective_upload, request.global_download, bool(request.flags & BT_SCHEDULER_NONDEFAULT_UPLOAD),
        bool(request.flags & BT_SCHEDULER_NONDEFAULT_DOWNLOAD)}, bool(request.flags & BT_SCHEDULER_ENABLED),
      bool(request.flags & BT_SCHEDULER_OVERRIDE_FORCED));
  if (!tick) { return BT_INVALID; }
  response.action_count = static_cast<std::uint32_t>(tick->actions.size());
  if (tick->actions.size() > response.action_capacity) { return BT_BUFFER_SMALL; }
  response.flags = (tick->nonDefaultUploadSet ? BT_SCHEDULER_NONDEFAULT_UPLOAD : 0)
      | (tick->nonDefaultDownloadSet ? BT_SCHEDULER_NONDEFAULT_DOWNLOAD : 0)
      | (tick->effectiveUpload ? BT_SCHEDULER_UPLOAD_CHANGED : 0)
      | (tick->globalDownload ? BT_SCHEDULER_DOWNLOAD_CHANGED : 0);
  response.effective_upload = tick->effectiveUpload.value_or(request.effective_upload);
  response.global_download = tick->globalDownload.value_or(request.global_download);
  std::size_t index = 0;
  for (const auto& action : tick->actions) {
    std::uint64_t token = 0;
    const auto parsed = std::from_chars(action.id.data(), action.id.data() + action.id.size(), token);
    if (parsed.ec != std::errc{}) { return BT_FAILED; }
    response.actions[index++] = {token, static_cast<std::uint32_t>(action.kind) + 1, 0};
  }
  return BT_OK;
}
bt_status call(void* state, std::uint32_t operation, const void* input, std::uint32_t inputSize,
    void* output, std::uint32_t outputSize) noexcept {
  if (!state || !static_cast<Instance*>(state)->running || !input || !output) { return BT_INVALID; }
  if (operation != BT_SCHEDULER_EVALUATE) { return BT_UNSUPPORTED; }
  if (inputSize != sizeof(bt_scheduler_request_v1) || outputSize != sizeof(bt_scheduler_result_v1)) { return BT_INVALID; }
  try {
    bt_scheduler_request_v1 request{}; bt_scheduler_result_v1 response{};
    std::memcpy(&request, input, sizeof(request)); std::memcpy(&response, output, sizeof(response));
    const auto status = evaluate(request, response);
    std::memcpy(output, &response, sizeof(response)); return status;
  } catch (...) { return BT_FAILED; }
}
void stop(void* state) noexcept { if (state) { static_cast<Instance*>(state)->running = false; } }
void destroy(void* state) noexcept { delete static_cast<Instance*>(state); }
const bt_plugin_v1 Api{sizeof(Api), 1, "SpeedScheduler", "0.1.0", BT_CAP_SCHEDULER, create, start, call, stop, destroy};
}
extern "C" BT_PLUGIN_EXPORT const bt_plugin_v1* biglybtc_plugin_entry(std::uint32_t requested) {
  return requested == 1 ? &Api : nullptr;
}
