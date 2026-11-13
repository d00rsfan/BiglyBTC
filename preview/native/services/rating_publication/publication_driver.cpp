// SPDX-License-Identifier: GPL-2.0-only
#include "publication_policy.hpp"
#include <algorithm>
#include <charconv>
#include <exception>
#include <iostream>
#include <map>
#include <new>
#include <sstream>
#include <utility>

namespace pub = biglybtc::plugins::rating::publication;
namespace {
std::string errorName(pub::Error error) {
  switch (error) {
    case pub::Error::Input: return "INPUT"; case pub::Error::Limit: return "LIMIT";
    case pub::Error::Memory: return "MEMORY"; case pub::Error::Expired: return "EXPIRED";
    case pub::Error::Closed: return "CLOSED"; case pub::Error::Foreign: return "FOREIGN";
    case pub::Error::AlreadyConsumed: return "CONSUMED"; case pub::Error::Stale: return "STALE";
  }
  std::terminate();
}
template<class T> T take(std::expected<T, pub::Error> result) {
  if (!result) { throw result.error(); } return std::move(*result);
}
void check(std::expected<void, pub::Error> result) { if (!result) { throw result.error(); } }
template<class T> T number(std::string_view token) {
  T result{}; const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) { throw pub::Error::Input; } return result;
}
bool bit(std::string_view value) { if (value != "0" && value != "1") { throw pub::Error::Input; } return value == "1"; }
template<class T> std::vector<T> list(std::string_view text) {
  std::vector<T> result;
  while (true) {
    if (result.size() == pub::Inputs::maximumScalars) { throw pub::Error::Limit; }
    const auto comma = text.find(','); result.push_back(number<T>(text.substr(0, comma)));
    if (comma == std::string_view::npos) { return result; } text.remove_prefix(comma + 1);
  }
}
std::string events(const pub::Receipt& receipt) {
  std::ostringstream out;
  for (std::size_t i = 0; i < receipt.size; ++i) {
    if (i != 0) { out << ';'; }
    const auto& e = receipt.events[i];
    const auto lock = e.criticalRegion ? 1 : 0;
    switch (e.kind) {
      case pub::EventKind::Removed: out << "REMOVED:" << e.label.data() << ':' << e.flag << ":L" << lock; break;
      case pub::EventKind::RandomBound: out << "RNG:" << e.id << ":L" << lock; break;
      case pub::EventKind::Choice: out << "CHOICE:" << e.id; break;
      case pub::EventKind::Dispatch: out << "DISPATCH:" << e.id << ":L" << lock; break;
      case pub::EventKind::Run: out << "RUN:" << e.id; break;
      case pub::EventKind::QueueSize: out << "QSIZE:" << e.id << ":L" << lock; break;
      case pub::EventKind::Log: out << "LOG:Publish queue size: " << e.id << ":L" << lock; break;
      case pub::EventKind::Write: out << "WRITE:" << e.label.data() << ":L" << lock; break;
      case pub::EventKind::Complete: out << "COMPLETE:" << e.id << ':' << e.number; break;
      case pub::EventKind::Clock: out << "CLOCK:" << e.id << ":L" << lock; break;
      case pub::EventKind::Timer: out << "TIMER:Rating:write:delay:" << e.when << ":L" << lock; break;
      case pub::EventKind::Fire: out << "FIRE:" << e.id; break;
    }
  }
  return receipt.size == 0 ? "-" : out.str();
}
struct Driver {
  pub::Limits limits;
  std::optional<pub::Policy> policy;
  pub::Inputs input;
  std::map<std::string, pub::Download> downloads;
  pub::CompletionMode mode = pub::CompletionMode::Deferred;
  explicit Driver(pub::Limits value) : limits(value) { reset(); }
  void reset() {
    auto next = take(pub::Policy::create(limits)); pub::Inputs nextInput;
    policy.emplace(std::move(next));
    downloads.clear(); input = std::move(nextInput); mode = pub::CompletionMode::Deferred;
  }
  const pub::Download& download(const std::string& name) const {
    const auto found = downloads.find(name); if (found == downloads.end()) { throw pub::Error::Input; } return found->second;
  }
  std::string snapshot(const pub::Receipt& receipt) const {
    const auto s = policy->snapshot();
    const auto label = [&](std::uint64_t id) -> std::string {
      const auto found = std::ranges::find(s.downloads, id, &pub::DownloadView::id);
      if (found == s.downloads.end()) { throw pub::Error::Stale; } return found->label;
    };
    const auto joined = [](std::vector<std::string> values, bool sort) {
      if (sort) { std::ranges::sort(values); } std::string result;
      for (const auto& value : values) { if (!result.empty()) { result += ','; } result += value; }
      return result.empty() ? std::string("-") : result;
    };
    std::vector<std::string> queue, rating, lookup;
    for (auto id : s.publications) { queue.push_back(label(id)); }
    for (const auto& d : s.downloads) {
      if (d.cachedRating) { rating.push_back(d.label); } if (d.cachedLookup) { lookup.push_back(d.label); }
    }
    std::ostringstream out;
    out << " | Q=" << joined(std::move(queue), false) << " R=" << joined(std::move(rating), true)
        << " K=" << joined(std::move(lookup), true) << " W=" << s.writeInProgress << " D=" << s.sourceDestroyed << " RUN=";
    for (auto id : s.dispatches) { out << id << ','; } out << " TIM=";
    for (const auto& t : s.timers) { out << t.id << ':' << t.when << ':' << t.fired << ','; } out << " CALL=";
    for (const auto& w : s.writes) { out << w.id << ':' << label(w.download) << ':' << w.completed << ','; }
    out << " RNG=" << input.choicesConsumed() << " CLOCK=" << input.timesConsumed() << " TRACE=" << events(receipt); return out.str();
  }
  std::string command(const std::string& line) {
    pub::Receipt receipt; std::string result;
    try {
      std::istringstream stream(line); std::vector<std::string> a; std::string word;
      while (stream >> word) { a.push_back(word); } if (a.empty()) { throw pub::Error::Input; }
      const auto& op = a[0];
      const auto size = [&](std::size_t n) { if (a.size() != n) { throw pub::Error::Input; } };
      if (op == "Z") { size(1); reset(); result = "Z"; }
      else if (op == "D") { size(3); auto d = take(policy->registerDownload(a[1], bit(a[2]))); downloads.emplace(a[1], d); result = "D"; }
      else if (op == "CACHE") { size(2); check(policy->seedCacheMembership(download(a[1]))); result = "CACHE"; }
      else if (op == "RNG") { size(2); const auto values = list<std::int32_t>(a[1]); check(input.setChoices(values)); result = "RNG"; }
      else if (op == "TIME") { size(2); const auto values = list<std::int64_t>(a[1]); check(input.setTimes(values)); result = "TIME"; }
      else if (op == "ADD") { size(3); receipt = take(policy->enqueue(download(a[1]), bit(a[2]), input)); result = "ADD"; }
      else if (op == "REMOVE") { size(2); receipt = take(policy->remove(download(a[1]))); result = "REMOVE"; }
      else if (op == "REMOVED") { size(3); check(policy->setRemoved(download(a[1]), bit(a[2]))); result = "REMOVED"; }
      else if (op == "STOPFLAG") { size(2); check(policy->setSourceDestroyed(bit(a[1]))); result = "STOPFLAG"; }
      else if (op == "STEP") { size(1); receipt = take(policy->dispatchOne(input, mode)); result = receipt.emptyDispatch ? "EMPTY" : "STEP"; }
      else if (op == "MODE") {
        size(2); if (a[1] != "SYNC" && a[1] != "HOLD") { throw pub::Error::Input; }
        mode = a[1] == "SYNC" ? pub::CompletionMode::Inline : pub::CompletionMode::Deferred; result = "MODE";
      } else if (op == "DONE") {
        size(3); if (a[2] != "N" && a[2] != "R") { throw pub::Error::Input; }
        receipt = take(policy->complete(take(policy->completion(number<std::uint64_t>(a[1]))), input)); result = "DONE";
      } else if (op == "FIRE") { size(2); receipt = take(policy->fire(take(policy->timer(number<std::uint64_t>(a[1]))))); result = "FIRE"; }
      else if (op == "SNAP") { size(1); result = "SNAP"; }
      else if (op == "CLOSE") { size(1); policy->close(); result = "CLOSE"; }
      else { throw pub::Error::Input; }
    } catch (pub::Error error) { result = "ERR " + errorName(error); }
    catch (const std::bad_alloc&) { result = "ERR MEMORY"; }
    catch (...) { result = "ERR INPUT"; }
    return result + snapshot(receipt);
  }
};
}
int main(int argc, char** argv) {
  try {
    pub::Limits limits;
    if (argc < 2 || std::string_view(argv[1]) != "--stdio") { return 2; }
    for (int i = 2; i < argc; i += 2) {
      if (i + 1 == argc) { return 2; }
      const auto value = number<std::size_t>(argv[i + 1]); const std::string_view option(argv[i]);
      if (option == "--downloads") { limits.downloads = value; }
      else if (option == "--publications") { limits.publications = value; }
      else if (option == "--dispatches") { limits.dispatches = value; }
      else if (option == "--writes") { limits.writeHistory = value; }
      else if (option == "--timers") { limits.timerHistory = value; } else { return 2; }
    }
    Driver driver(limits); std::string line;
    while (true) {
      line.clear(); bool any = false, tooLong = false; char c{};
      while (std::cin.get(c)) {
        any = true; if (c == '\n') { break; }
        if (line.size() == 8192) { tooLong = true; } else if (!tooLong) { line += c; }
      }
      if (!any) { break; }
      if (tooLong) { std::cout << "ERR LIMIT\n"; }
      else { std::cout << driver.command(line) << '\n'; }
      std::cout.flush();
    }
    return 0;
  } catch (...) { return 2; }
}
