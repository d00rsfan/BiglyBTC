#include "biglybtc/queue_policy.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc::core;
using Values = std::map<std::string, std::int64_t>;
std::int64_t integer(std::string_view text) {
  std::int64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) { throw std::runtime_error("invalid integer"); }
  return value;
}
std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> result;
  std::size_t start = 0;
  for (;;) {
    const auto end = line.find('\t', start);
    result.push_back(line.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos) { return result; }
    start = end + 1;
  }
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ":" + std::string(value); }
std::int32_t narrow(std::int64_t value) {
  if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
    throw std::runtime_error("input outside signed32");
  }
  return static_cast<std::int32_t>(value);
}
QueueTorrentState state(std::int64_t value) {
  switch (value) {
    case 1: return QueueTorrentState::Waiting;
    case 2: return QueueTorrentState::Preparing;
    case 3: return QueueTorrentState::Waiting; // Ready is waiting-to-run in native facts; not compared as an effect.
    case 4: return QueueTorrentState::Downloading;
    case 5: return QueueTorrentState::Seeding;
    case 6: return QueueTorrentState::Stopping;
    case 7: return QueueTorrentState::Stopped;
    case 8: return QueueTorrentState::Failed;
    case 9: return QueueTorrentState::Queued;
    default: throw std::runtime_error("unknown original state");
  }
}
void run(const std::string& id, const std::map<std::string, std::string>& config,
    const Values& values, std::int64_t wall, std::int64_t mono) {
  std::string encoded = "d";
  for (const auto& [key, value] : config) { encoded += bytes(key); encoded += value; }
  encoded += 'e';
  auto document = Document::parse(std::move(encoded));
  if (!document) { throw std::runtime_error("fixture configuration parse failed"); }
  const auto policy = readQueuePolicy(&*document);
  if (!policy) {
    std::cout << "REJECT\t" << id << "\tconfiguration\t" << policy.error().key << '\n';
    return;
  }
  const auto get = [&values](const char* key) { return values.at(key); };
  QueueTorrentFacts facts;
  facts.token = 1;
  const auto position = get("position");
  if (position <= 0 || position > std::numeric_limits<std::int32_t>::max()) { throw std::runtime_error("position domain"); }
  facts.position = static_cast<std::uint32_t>(position);
  facts.state = state(get("state"));
  facts.complete = get("complete") != 0;
  facts.persistent = get("persistent") != 0;
  facts.forceStart = get("forceStart") != 0;
  facts.moving = get("moving") != 0;
  const auto flags = get("transientFlags");
  facts.externallyFirstPriority = (flags & 3) != 0;
  facts.externallyNotFirstPriority = (flags & 4) != 0;
  facts.shareRatio = narrow(get("shareRatio"));
  facts.minimumShareRatio = narrow(get("minimumShareRatio"));
  facts.maximumShareRatio = narrow(get("maximumShareRatio"));
  facts.secondsDownloading = get("secondsDownloading");
  facts.secondsSeeding = get("secondsSeeding");
  facts.secondsSinceLastUpload = get("secondsSinceLastUpload");
  // Fixed fixture clocks are nonnegative and start no later than wall. These
  // translations supply elapsed host facts; they contain no ranking formula.
  if (wall < get("startedMillis") || wall < get("seedingStartedMillis")) { throw std::runtime_error("fixture time domain"); }
  facts.runningMillis = wall - get("startedMillis");
  facts.seedingMillis = wall - get("seedingStartedMillis");
  if (get("lastActiveMillis") != 0 || get("completedMillis") != 0) { throw std::runtime_error("uncontrolled original clock branch"); }
  facts.swarm = QueueSwarm{narrow(get("seeds")), narrow(get("peers")), get("scrapeMillis") > 0};
  // Every current fixture uses an older scrape than the seeding start and no
  // Downloading scrape, so raw counts are already the original excludes-self
  // counts. Unknown scrape remains explicitly invalid, never observed zero.
  if (facts.state == QueueTorrentState::Downloading && get("scrapeMillis") > get("startedMillis")) {
    throw std::runtime_error("self-count subtraction fixture not admitted");
  }
  facts.connectedLeechers = 0;
  facts.downloadBytesPerSecond = get("downloadRate");
  facts.uploadBytesPerSecond = get("uploadRate");
  // Original core PeerManager provider is null. These explicit zero contribution
  // facts reproduce its base minimum-time branch, not a measured global rate.
  facts.smoothedUploadBytesPerSecond = 0;
  facts.diskReadLatencyMillis = 0;
  facts.diskWriteLatencyMillis = 0;
  QueueContext context;
  context.nowMillis = mono;
  context.ownerStartedMillis = 0;
  context.smoothedUploadBytesPerSecond = 0;
  const auto result = evaluateQueue(*policy, std::span<const QueueTorrentFacts>(&facts, 1), context);
  if (!result || result->decisions.size() != 1) {
    std::cout << "REJECT\t" << id << "\tfacts\t" << (result ? "size" : result.error().key) << '\n';
    return;
  }
  const auto& decision = result->decisions.front();
  std::cout << "OBS\t" << id << "\tseedingRank\t" << decision.seedingRank << '\n';
  if (decision.firstPriorityKnown) {
    std::cout << "OBS\t" << id << "\tfirstPriority\t" << (decision.firstPriority ? 1 : 0) << '\n';
  } else { std::cout << "UNKNOWN\t" << id << "\tfirstPriority\n"; }
}
} // namespace

int main() {
  try {
    std::string id;
    std::map<std::string, std::string> config;
    Values facts;
    std::int64_t wall = 0, mono = 0;
    std::size_t count = 0;
    for (std::string line; std::getline(std::cin, line);) {
      if (line.empty() || line.front() == '#') { continue; }
      const auto fields = split(line);
      if (fields[0] == "CASE" && fields.size() == 2 && id.empty()) {
        id = fields[1]; config.clear(); facts.clear();
      } else if (fields[0] == "CFG" && fields.size() == 4 && !id.empty()) {
        if (config.contains(fields[1])) { throw std::runtime_error("duplicate config"); }
        if (fields[2] == "F") { config.emplace(fields[1], bytes(fields[3])); }
        else if (fields[2] == "I" || fields[2] == "B") {
          const auto value = integer(fields[3]);
          if (fields[2] == "B" && value != 0 && value != 1) { throw std::runtime_error("boolean domain"); }
          config.emplace(fields[1], "i" + std::to_string(value) + "e");
        } else { throw std::runtime_error("config type"); }
      } else if (fields[0] == "FACT" && fields.size() == 3 && !id.empty()) {
        if (!facts.emplace(fields[1], integer(fields[2])).second) { throw std::runtime_error("duplicate fact"); }
      } else if (fields[0] == "CLOCK" && fields.size() == 3 && !id.empty()) {
        wall = integer(fields[1]); mono = integer(fields[2]);
      } else if (fields[0] == "END" && fields.size() == 1 && !id.empty() && ++count <= 1000) {
        run(id, config, facts, wall, mono); id.clear();
      } else { throw std::runtime_error("invalid fixture command"); }
    }
    if (!id.empty() || count == 0) { throw std::runtime_error("incomplete fixture"); }
    std::cout << "PASS_QUEUE_RANK_NATIVE\t" << count << '\n';
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
