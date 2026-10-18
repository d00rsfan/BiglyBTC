#include "biglybtc/saved_dnd.hpp"
#include "biglybtc/state_cache.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Source-reading-derived expectations and explicit native rejection policy.
// No original Java execution/equality, files, managers, recovery, live disk,
// public assumedComplete, pane membership, per-file progress or client claim.
namespace {
using namespace biglybtc::core;
using I = std::int64_t;
using O = std::optional<I>;
std::size_t assertions = 0;
std::size_t cases = 0;
void check(bool value, const char* message) {
  ++assertions;
  if (!value) { throw std::runtime_error(message); }
}
template<class Function>
void run(std::string_view name, Function&& function) {
  try { function(); ++cases; }
  catch (const std::exception& failure) {
    throw std::runtime_error(std::string(name) + ": " + failure.what());
  }
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(I value) { return 'i' + std::to_string(value) + 'e'; }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string result = "d";
  for (const auto& [key, value] : fields) { result += bytes(key) + value; }
  return result + 'e';
}
Document document(std::string input) {
  auto result = Document::parse(std::move(input));
  check(result.has_value(), "Generated input must be valid bounded bencoding");
  return std::move(*result);
}
SavedDndFacts facts(std::string input) {
  auto source = document(std::move(input));
  const auto before = source.serialize();
  auto value = readSavedDndFacts(&source.root());
  check(source.serialize() == before, "Facts must not edit input bytes");
  return value; // Owned primitive facts outlive this Document.
}
SavedDndFacts attributes(const O& skipped, const O& skippedDownloaded, const O& flags = {}) {
  std::string input = "d";
  if (skipped) { input += bytes("skipfss") + integer(*skipped); }
  if (skippedDownloaded) { input += bytes("skipbdl") + integer(*skippedDownloaded); }
  if (flags) { input += bytes("parameters") + map({{"dndflags", integer(*flags)}}); }
  return facts(input + 'e');
}
bool issue(const SavedDndProgress& value, SavedDndIssue expected) { return hasSavedDndIssue(value.diagnostics, expected); }
bool issue(const SavedDndFacts& value, SavedDndIssue expected) { return hasSavedDndIssue(value.diagnostics, expected); }
void metrics(const SavedDndProgress& value, const O& size, const O& remaining, const std::optional<std::int32_t>& done) {
  check(value.selectedBytes == size, "Selected-size presence/value mismatch");
  check(value.selectedRemaining == remaining, "Selected-remaining presence/value mismatch");
  check(value.percentThousandths == done, "Selected-Done presence/value mismatch");
}
std::string entry(std::string_view identity, std::string_view name, const std::optional<std::string>& attrs) {
  auto result = map({{"hash", bytes(identity)}, {"name", bytes(name)}, {"utf8name", bytes("UTF-8 metadata")},
      {"size", integer(100)}, {"fc", integer(2)}, {"tt", integer(1)}, {"priv", integer(1)}});
  if (attrs) {
    result.pop_back();
    result += bytes("attributes") + *attrs + 'e';
  }
  return result;
}
std::string cache(std::string entries) { return map({{"state", 'l' + entries + 'e'}, {"unknown", bytes(std::string("\0\xff", 2))}}); }
StateCache readOwned(std::string input) {
  auto source = document(std::move(input));
  const auto before = source.serialize();
  auto result = readStateCache(source);
  check(result.has_value(), "Generated cache must project");
  check(source.serialize() == before, "Cache projection must preserve all bytes/unknown keys");
  return std::move(*result);
}
} // namespace

static_assert(noexcept(readSavedDndFacts(nullptr)));
static_assert(noexcept(projectSavedDndProgress({}, {}, SavedDndFacts{})));

int main() {
  try {
    run("absent-attributes-are-not-empty-defaults", [] {
      const auto value = readSavedDndFacts(nullptr);
      check(!value.attributesPresent && !value.attributesDictionary, "Missing attributes presence");
      check(!value.skippedBytes && !value.skippedDownloadedBytes && !value.dndFlags, "No invented raw fields");
      check(value.defaults == 0, "Absent attributes must not manufacture source defaults");
      check(issue(value, SavedDndIssue::AttributesMissing), "Missing-attributes diagnostic");
      const auto result = projectSavedDndProgress(100, 100, value);
      metrics(result, {}, {}, {});
      check(!result.cachedHasDnd && !result.cachedCompleteNoDnd, "Missing flags are unknown, even full complete");
      check(!result.controllerNoDiskCompleteExcludingDnd && !result.controllerNoDiskCompleteIncludingDnd,
          "Do not invent controller cache initialization");
    });
    run("empty-attributes-tag-original-minus-one-and-zero-defaults", [] {
      const auto value = facts("de");
      check(value.attributesPresent && value.attributesDictionary, "Valid empty attributes");
      check(!value.skippedBytesPresent && !value.skippedDownloadedBytesPresent, "Default is not explicit raw field");
      check(!value.skippedBytes && !value.skippedDownloadedBytes, "Raw defaults remain absent");
      check(hasSavedDndDefault(value.defaults, SavedDndDefault::SkippedBytes), "Missing K defaults to source -1");
      check(hasSavedDndDefault(value.defaults, SavedDndDefault::SkippedDownloadedBytes), "Missing B defaults to source zero");
      const auto result = projectSavedDndProgress(100, 25, value);
      metrics(result, {}, {}, {});
      check(issue(result, SavedDndIssue::SkippedUnresolved), "Missing K requires unavailable original recovery");
      check(result.defaults == value.defaults, "Projection retains default provenance");
    });
    run("missing-B-default-zero-is-distinct-from-stored-zero", [] {
      const auto missing = attributes(20, {}, 3);
      const auto stored = attributes(20, 0, 3);
      check(!missing.skippedDownloadedBytesPresent && !missing.skippedDownloadedBytes, "Missing raw B");
      check(stored.skippedDownloadedBytesPresent && stored.skippedDownloadedBytes == 0, "Stored zero raw B");
      check(hasSavedDndDefault(missing.defaults, SavedDndDefault::SkippedDownloadedBytes), "Defaulted B provenance");
      check(!hasSavedDndDefault(stored.defaults, SavedDndDefault::SkippedDownloadedBytes), "Stored B has no default tag");
      metrics(projectSavedDndProgress(100, 25, missing), 80, 55, 312);
      metrics(projectSavedDndProgress(100, 25, stored), 80, 55, 312);
    });
    run("missing-K-with-explicit-B-keeps-only-recovery-default", [] {
      const auto raw = attributes({}, 0, 3);
      check(!raw.skippedBytesPresent && !raw.skippedBytes, "Missing K does not become a raw -1");
      check(raw.skippedDownloadedBytesPresent && raw.skippedDownloadedBytes == 0, "Stored B zero survives");
      check(hasSavedDndDefault(raw.defaults, SavedDndDefault::SkippedBytes)
          && !hasSavedDndDefault(raw.defaults, SavedDndDefault::SkippedDownloadedBytes), "Defaults retain per-field provenance");
      const auto result = projectSavedDndProgress(100, 25, raw);
      metrics(result, {}, {}, {});
      check(issue(result, SavedDndIssue::SkippedUnresolved), "No recovery or no-DND assumption from missing K");
    });
    for (I value : {I{-1}, I{-7}, std::numeric_limits<I>::min()}) {
      run("negative-K-needs-recovery-" + std::to_string(value), [value] {
        const auto raw = attributes(value, 0, 3);
        check(raw.skippedBytesPresent && raw.skippedBytes == value, "Preserve negative raw K");
        check(!hasSavedDndDefault(raw.defaults, SavedDndDefault::SkippedBytes), "Explicit negative K is not absence");
        const auto result = projectSavedDndProgress(100, 50, raw);
        metrics(result, {}, {}, {});
        check(issue(result, SavedDndIssue::SkippedUnresolved), "Original recalculation is unavailable");
        check(result.controllerNoDiskCompleteExcludingDnd == true && result.controllerNoDiskCompleteIncludingDnd == false,
            "Valid flag-only candidates survive unknown byte metrics");
      });
    }

    struct NormalCase { const char* name; I total, completed, skipped, downloaded, size, remaining; std::int32_t done; };
    const NormalCase normal[]{
        {"none-skipped-start", 100, 0, 0, 0, 100, 100, 0},
        {"none-skipped-quarter", 100, 25, 0, 0, 100, 75, 250},
        {"some-skipped-selected-complete", 100, 60, 40, 0, 60, 0, 1000},
        {"some-skipped-both-selected-and-skipped-downloaded", 100, 60, 40, 20, 60, 20, 666},
        {"only-skipped-bytes-downloaded", 100, 40, 40, 40, 60, 60, 0},
        {"full-complete", 100, 100, 20, 20, 80, 0, 1000},
        {"all-skipped-incomplete-full-torrent", 100, 0, 100, 0, 0, 0, 1000},
        {"binary32-half-rounds-up-not-double-499", 16777217, 8388608, 0, 0, 16777217, 8388609, 500},
        {"binary32-rounded-full-is-corrected-999", 16777217, 16777216, 0, 0, 16777217, 1, 999},
        {"int64-max-complete", std::numeric_limits<I>::max(), std::numeric_limits<I>::max(), 0, 0,
            std::numeric_limits<I>::max(), 0, 1000},
        {"int64-max-unstarted", std::numeric_limits<I>::max(), 0, 0, 0,
            std::numeric_limits<I>::max(), std::numeric_limits<I>::max(), 0}};
    for (const auto& test : normal) {
      run(test.name, [&test] {
        const auto raw = attributes(test.skipped, test.downloaded, 3);
        const auto result = projectSavedDndProgress(test.total, test.completed, raw);
        metrics(result, test.size, test.remaining, test.done);
        check(result.diagnostics == 0 && result.defaults == 0, "Valid explicit tuple has no malformed/default provenance");
        check(result.cachedHasDnd == true && result.cachedCompleteNoDnd == true, "Literal cached flag facts");
        check(result.controllerNoDiskCompleteExcludingDnd == true && result.controllerNoDiskCompleteIncludingDnd == false,
            "No-DiskManager DND candidates are separate from full/selected metrics");
      });
    }

    run("selected-size-known-without-C", [] {
      const auto result = projectSavedDndProgress(100, {}, attributes(20, 0));
      metrics(result, 80, {}, {});
      check(issue(result, SavedDndIssue::CompletedMissing), "Missing C provenance");
    });
    run("zero-selected-Done-before-C-and-malformed-B", [] {
      const auto raw = facts(map({{"skipfss", integer(100)}, {"skipbdl", "3:bad"}}));
      const auto result = projectSavedDndProgress(100, {}, raw);
      metrics(result, 0, {}, 1000);
      check(issue(result, SavedDndIssue::CompletedMissing), "Missing C remains diagnostic");
      check(issue(result, SavedDndIssue::SkippedDownloadedBytesType), "Bad B remains diagnostic despite early return");
    });
    run("zero-selected-Done-before-invalid-C-and-negative-B", [] {
      const auto result = projectSavedDndProgress(100, -1, attributes(100, -1, 3));
      metrics(result, 0, {}, 1000);
      check(issue(result, SavedDndIssue::CompletedRange), "Invalid C is not repaired by Done");
      check(issue(result, SavedDndIssue::SkippedDownloadedRange), "Invalid B is not repaired by Done");
    });
    run("K-greater-than-T-is-native-unknown-not-source-negative-size", [] {
      const auto result = projectSavedDndProgress(1, 0, attributes(std::numeric_limits<I>::max(), 0, 3));
      metrics(result, {}, {}, {});
      check(issue(result, SavedDndIssue::SkippedRange), "Strict native K>T rejection remains explicit");
      check(result.controllerNoDiskCompleteExcludingDnd == true, "Independent cached controller fact survives");
    });
    for (const O& total : {O{}, O{0}, O{-1}, O{std::numeric_limits<I>::min()}}) {
      run("unresolved-total-" + (total ? std::to_string(*total) : "absent"), [&total] {
        const auto result = projectSavedDndProgress(total, 0, attributes(0, 0, 3));
        metrics(result, {}, {}, {});
        check(issue(result, SavedDndIssue::TotalUnresolved), "Nonpositive/missing cached T needs original fixup");
        check(result.controllerNoDiskCompleteExcludingDnd == true && result.controllerNoDiskCompleteIncludingDnd == false,
            "Flags-only candidates do not require T/C");
      });
    }
    for (I completed : {I{-1}, std::numeric_limits<I>::min(), I{101}, std::numeric_limits<I>::max()}) {
      run("invalid-C-" + std::to_string(completed), [completed] {
        const auto result = projectSavedDndProgress(100, completed, attributes(20, 0, 0));
        metrics(result, 80, {}, {});
        check(issue(result, SavedDndIssue::CompletedRange), "C range is native policy, not clamped bytes");
        check(!result.controllerNoDiskCompleteExcludingDnd && !result.controllerNoDiskCompleteIncludingDnd,
            "No-DND candidates need valid full remaining");
      });
    }
    for (I downloaded : {I{-1}, std::numeric_limits<I>::min(), I{21}, std::numeric_limits<I>::max()}) {
      run("invalid-B-" + std::to_string(downloaded), [downloaded] {
        const auto result = projectSavedDndProgress(100, 50, attributes(20, downloaded, 3));
        metrics(result, 80, {}, {});
        check(issue(result, SavedDndIssue::SkippedDownloadedRange), "B range invalid; retain independent S");
        check(result.controllerNoDiskCompleteExcludingDnd == true, "Invalid byte tuple must not erase valid flags");
      });
    }
    struct CrossCase { const char* name; I completed, downloaded; };
    for (const auto& test : {CrossCase{"C-less-than-B", 10, 11}, CrossCase{"C-minus-B-greater-than-S", 90, 0}}) {
      run(test.name, [&test] {
        const auto result = projectSavedDndProgress(100, test.completed, attributes(20, test.downloaded, 3));
        metrics(result, 80, {}, {});
        check(issue(result, SavedDndIssue::CountersInconsistent), "Cross-file counters require native unknown publication");
        check(result.cachedHasDnd == true && result.cachedCompleteNoDnd == true, "Retain literal flag facts");
      });
    }

    run("flags-absent-are-not-explicit-zero", [] {
      const auto absent = attributes(0, 0);
      const auto explicitZero = attributes(0, 0, 0);
      check(!absent.dndFlagsPresent && !absent.dndFlags, "Absent cached parameter has no initialized facts");
      check(explicitZero.parametersPresent && explicitZero.parametersDictionary && explicitZero.dndFlagsPresent
          && explicitZero.dndFlags == 0, "Explicit zero is present and initialized");
      const auto unknown = projectSavedDndProgress(100, 100, absent);
      metrics(unknown, 100, 0, 1000);
      check(!unknown.cachedHasDnd && !unknown.cachedCompleteNoDnd, "Done1000 is not a flag default");
      check(!unknown.controllerNoDiskCompleteExcludingDnd && !unknown.controllerNoDiskCompleteIncludingDnd,
          "Done1000 cannot initialize a controller candidate");
      const auto known = projectSavedDndProgress(100, 100, explicitZero);
      check(known.cachedHasDnd == false && known.cachedCompleteNoDnd == false, "Literal F0 masks");
      check(known.controllerNoDiskCompleteExcludingDnd == true && known.controllerNoDiskCompleteIncludingDnd == true,
          "F0 uses exact full remaining zero");
    });
    run("empty-parameters-do-not-initialize-cached-flags", [] {
      const auto raw = facts(map({{"skipfss", integer(0)}, {"skipbdl", integer(0)}, {"parameters", "de"}}));
      check(raw.parametersPresent && raw.parametersDictionary && !raw.dndFlagsPresent && !raw.dndFlags,
          "A parameter map is not presence of its dndflags key");
      const auto result = projectSavedDndProgress(100, 100, raw);
      metrics(result, 100, 0, 1000);
      check(issue(result, SavedDndIssue::FlagsMissing), "Missing key retains explicit unknown provenance");
      check(!result.cachedHasDnd && !result.cachedCompleteNoDnd && !result.controllerNoDiskCompleteExcludingDnd
          && !result.controllerNoDiskCompleteIncludingDnd, "No flags or completion candidate from empty parameters");
    });
    struct FlagCase { const char* name; I flags; bool hasDnd, complete, excluding, including; };
    const FlagCase flagCases[]{
        {"F0-noDND-incomplete", 0, false, false, false, false},
        {"F1-DND-incomplete", 1, true, false, false, false},
        {"F2-noDND-cachedcomplete-still-needs-fullR", 2, false, true, false, false},
        {"F3-selectedcomplete-fullincomplete", 3, true, true, true, false},
        {"extra-high-bits", 1099511627779LL, true, true, true, false},
        {"negative-all-bits", -1, true, true, true, false},
        {"sign-bit-only", std::numeric_limits<I>::min(), false, false, false, false},
        {"sign-bit-plus-low-bits", std::numeric_limits<I>::min() + 3, true, true, true, false}};
    for (const auto& test : flagCases) {
      run(test.name, [&test] {
        const auto raw = attributes(20, 0, test.flags);
        check(raw.dndFlags == test.flags, "Do not narrow raw signed flags to int32");
        const auto result = projectSavedDndProgress(100, 50, raw);
        check(result.cachedHasDnd == test.hasDnd && result.cachedCompleteNoDnd == test.complete, "Literal mask facts");
        check(result.controllerNoDiskCompleteExcludingDnd == test.excluding
            && result.controllerNoDiskCompleteIncludingDnd == test.including, "No-DiskManager controller truth table");
      });
    }
    run("F2-fullcomplete-uses-fullR-regardless-cached-bit", [] {
      const auto result = projectSavedDndProgress(100, 100, attributes(0, 0, 2));
      check(result.cachedHasDnd == false && result.cachedCompleteNoDnd == true, "F2 raw bit facts");
      check(result.controllerNoDiskCompleteExcludingDnd == true && result.controllerNoDiskCompleteIncludingDnd == true,
          "No-DND completion is exact full-R equality");
    });
    run("F1-and-F3-shortcut-with-no-T-or-C", [] {
      const auto incomplete = projectSavedDndProgress({}, {}, attributes({}, {}, 1));
      const auto complete = projectSavedDndProgress({}, {}, attributes({}, {}, 3));
      metrics(incomplete, {}, {}, {});
      metrics(complete, {}, {}, {});
      check(incomplete.controllerNoDiskCompleteExcludingDnd == false && incomplete.controllerNoDiskCompleteIncludingDnd == false,
          "F1 independent knownfalse candidates");
      check(complete.controllerNoDiskCompleteExcludingDnd == true && complete.controllerNoDiskCompleteIncludingDnd == false,
          "F3 excluding true, including false without READY disk");
    });

    struct MalformedCase { const char* name; std::string input; SavedDndIssue diagnostic; bool sizeKnown, remainingKnown, flagsKnown; };
    const MalformedCase malformed[]{
        {"wrong-attribute-shape", "i5e", SavedDndIssue::AttributesType, false, false, false},
        {"wrong-parameters-shape", map({{"skipfss", integer(20)}, {"skipbdl", integer(0)}, {"parameters", "le"}}),
            SavedDndIssue::ParametersType, true, true, false},
        {"wrong-K-type-not-numeric-text", map({{"skipfss", "2:20"}, {"skipbdl", integer(0)}, {"parameters", map({{"dndflags", integer(3)}})}}),
            SavedDndIssue::SkippedBytesType, false, false, true},
        {"wrong-B-type-not-numeric-text", map({{"skipfss", integer(20)}, {"skipbdl", "1:0"}, {"parameters", map({{"dndflags", integer(3)}})}}),
            SavedDndIssue::SkippedDownloadedBytesType, true, false, true},
        {"wrong-F-type-not-source-invalid-default-zero", map({{"skipfss", integer(20)}, {"skipbdl", integer(0)},
            {"parameters", map({{"dndflags", "1:3"}})}}), SavedDndIssue::DndFlagsType, true, true, false}};
    for (const auto& test : malformed) {
      run(test.name, [&test] {
        const auto raw = facts(test.input);
        check(issue(raw, test.diagnostic), "Preserve malformed-type diagnostic");
        const auto result = projectSavedDndProgress(100, 25, raw);
        check(result.selectedBytes.has_value() == test.sizeKnown, "Malformed field affects only dependent S");
        check(result.selectedRemaining.has_value() == test.remainingKnown, "Malformed field affects only dependent Q");
        check(result.percentThousandths.has_value() == test.remainingKnown, "Positive-S Done follows Q availability");
        check(result.cachedHasDnd.has_value() == test.flagsKnown, "Malformed byte field does not erase flags");
        check(result.cachedCompleteNoDnd.has_value() == test.flagsKnown, "Malformed flag is unknown, not false");
        check(issue(result, test.diagnostic), "Projection retains parse diagnostic");
      });
    }
    run("owned-facts-do-not-alias-edited-document", [] {
      auto source = document(map({{"skipfss", integer(20)}, {"skipbdl", integer(0)}, {"parameters", map({{"dndflags", integer(3)}})},
          {"unknown", bytes(std::string("\0\xff", 2))}}));
      const auto before = source.serialize();
      const auto owned = readSavedDndFacts(&source.root());
      check(source.serialize() == before, "Reading facts is immutable");
      check(source.setInteger("skipfss", 90), "Explicit fixture edit succeeds");
      check(source.serialize() != before, "Fixture actually changed");
      check(owned.skippedBytes == 20 && owned.skippedDownloadedBytes == 0 && owned.dndFlags == 3, "Facts own all primitive values");
      metrics(projectSavedDndProgress(100, 25, owned), 80, 55, 312);
      metrics(projectSavedDndProgress(100, 25, readSavedDndFacts(&source.root())), 10, {}, {});
    });

    const std::string identity(20, '\x7f');
    const auto hexIdentity = TorrentHash::parse(identity)->hex();
    run("cache-absent-attributes-preserves-old-metadata", [&] {
      const auto value = readOwned(cache(entry(identity, "original", {})));
      check(value.issues.empty() && value.torrents.size() == 1, "Missing DND attrs do not poison legacy metadata");
      const auto& row = value.torrents.at(hexIdentity);
      check(row.name == "original" && row.utf8Name == "UTF-8 metadata" && row.bytes == 100 && row.files == 2 && row.type == 1
          && row.privateTorrent, "Old cached fields remain intact");
      check(!row.savedDnd.attributesPresent && issue(row.savedDnd, SavedDndIssue::AttributesMissing), "Absent-attribute knowledge retained");
    });
    run("cache-missing-parameters-retains-name-and-default-provenance", [&] {
      const auto value = readOwned(cache(entry(identity, "original", map({{"displayname", bytes("visible")}, {"skipfss", integer(20)}}))));
      check(value.issues.empty() && value.torrents.size() == 1, "Missing parameters are soft unknown flags");
      const auto& row = value.torrents.at(hexIdentity);
      check(row.displayName == "visible" && row.savedDnd.skippedBytes == 20 && !row.savedDnd.parametersPresent, "Known metadata and K retained");
      check(hasSavedDndDefault(row.savedDnd.defaults, SavedDndDefault::SkippedDownloadedBytes), "Missing cached B default is explicit");
      metrics(projectSavedDndProgress(row.bytes, 25, row.savedDnd), 80, 55, 312);
    });
    run("cache-malformed-new-fields-do-not-drop-valid-metadata", [&] {
      const auto attrs = map({{"displayname", bytes("visible")}, {"skipfss", "3:bad"}, {"skipbdl", "le"},
          {"parameters", map({{"dndflags", "de"}})}, {"flags", bytes("outside-this-slice")}});
      const auto value = readOwned(cache(entry(identity, "original", attrs)));
      check(value.issues.empty() && value.torrents.size() == 1, "Only DND fields are malformed, not the base cache entry");
      const auto& row = value.torrents.at(hexIdentity);
      check(row.name == "original" && row.displayName == "visible" && row.bytes == 100, "Base metadata is not poisoned");
      check(row.savedDnd.skippedBytesPresent && row.savedDnd.skippedDownloadedBytesPresent && row.savedDnd.dndFlagsPresent,
          "Malformed present fields keep presence");
      check(!row.savedDnd.skippedBytes && !row.savedDnd.skippedDownloadedBytes && !row.savedDnd.dndFlags, "Malformed values are not parsed/defaulted");
      check(issue(row.savedDnd, SavedDndIssue::SkippedBytesType) && issue(row.savedDnd, SavedDndIssue::SkippedDownloadedBytesType)
          && issue(row.savedDnd, SavedDndIssue::DndFlagsType), "All soft DND diagnostics survive");
    });
    run("cache-wrong-parameters-shape-is-soft-DND-issue", [&] {
      const auto attrs = map({{"displayname", bytes("visible")}, {"skipfss", integer(20)}, {"skipbdl", integer(0)}, {"parameters", "i7e"}});
      const auto value = readOwned(cache(entry(identity, "original", attrs)));
      check(value.issues.empty() && value.torrents.size() == 1, "Bad new parameters shape does not discard metadata");
      const auto& row = value.torrents.at(hexIdentity);
      check(row.displayName == "visible" && issue(row.savedDnd, SavedDndIssue::ParametersType), "Preserve display name and soft issue");
      metrics(projectSavedDndProgress(row.bytes, 25, row.savedDnd), 80, 55, 312);
    });
    run("cache-original-invalid-attributes-and-display-name-still-reject-entry", [&] {
      const auto wrongShape = entry(identity, "bad-shape", "i4e");
      const auto wrongDisplay = entry(identity, "bad-name", map({{"displayname", integer(7)}, {"skipfss", integer(0)}}));
      const auto value = readOwned(cache(wrongShape + wrongDisplay));
      check(value.torrents.empty() && value.issues.size() == 2, "Do not weaken old metadata validation");
      check(value.issues[0].sourceIndex == 0 && value.issues[0].error == CacheError::Entry, "Original attributes error remains indexed");
      check(value.issues[1].sourceIndex == 1 && value.issues[1].error == CacheError::Text, "Original displayname error remains indexed");
    });
    run("cache-last-valid-entry-wins-with-soft-new-field-errors", [&] {
      const auto first = entry(identity, "first", map({{"displayname", bytes("first-display")}, {"skipfss", integer(20)}}));
      const auto last = entry(identity, "last", map({{"displayname", bytes("last-display")}, {"skipfss", "3:bad"}}));
      const auto rejected = entry(identity, "rejected", map({{"displayname", integer(3)}}));
      const auto value = readOwned(cache(first + last + rejected));
      check(value.torrents.size() == 1 && value.issues.size() == 1, "Coalesce valid duplicates before rejecting later base-invalid row");
      const auto& row = value.torrents.at(hexIdentity);
      check(row.name == "last" && row.displayName == "last-display" && issue(row.savedDnd, SavedDndIssue::SkippedBytesType),
          "Last accepted row owns both old metadata and new diagnostic facts");
      check(value.issues[0].sourceIndex == 2 && value.issues[0].error == CacheError::Text, "Later rejected entry does not replace valid row");
    });
    run("old-cache-caps-and-source-preservation-unchanged", [&] {
      const auto raw = cache(entry(identity, "first", map({{"skipfss", integer(20)}}))
          + entry(identity, "other", map({{"skipfss", integer(30)}})));
      auto source = document(raw);
      const auto capped = readStateCache(source, 1);
      check(!capped && capped.error() == CacheError::Limit, "Entry cap precedes identity coalescing");
      const auto shortNames = readStateCache(source, 2, 2);
      check(shortNames && shortNames->torrents.empty() && shortNames->issues.size() == 2, "Old name-allocation cap still applies");
      check(source.serialize() == raw, "Reduced limits do not rewrite source document");
    });
    std::cout << "PASS saved-DND native/source-reading-derived cases=" << cases << " assertions=" << assertions
              << "; no original-runtime equality or live completion credit\n";
  } catch (const std::exception& failure) {
    std::cerr << "FAIL saved-DND " << failure.what() << '\n';
    return 1;
  }
}
