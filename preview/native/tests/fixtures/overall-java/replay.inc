// Derived only from pinned literal inputs and actual original Java stdout.
// Four failed Java construction/provider cases are outside the typed reducer API.
// No provider registration/count, filesystem, timer or physical save equality.

void javaCase1() {
  std::string_view context = "fresh_empty_root";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "fresh_empty_root:1:0:construct";
  context = "fresh_empty_root:2:0:activate_captured_lifecycle";
  context = "fresh_empty_root:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "fresh_empty_root:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "fresh_empty_root:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase2() {
  std::string_view context = "no_file_map_output";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "no_file_map_output:1:0:construct";
  context = "no_file_map_output:2:0:activate_captured_lifecycle";
  context = "no_file_map_output:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "no_file_map_output:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "no_file_map_output:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase3() {
  std::string_view context = "unrecoverable_corrupt_map_output";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "unrecoverable_corrupt_map_output:1:0:construct";
  context = "unrecoverable_corrupt_map_output:2:0:activate_captured_lifecycle";
  context = "unrecoverable_corrupt_map_output:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "unrecoverable_corrupt_map_output:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "unrecoverable_corrupt_map_output:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase4() {
  std::string_view context = "null_all";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "null_all:1:0:construct";
  context = "null_all:2:0:activate_captured_lifecycle";
  context = "null_all:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "null_all:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "null_all:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase5() {
  std::string_view context = "all_thirteen_distinct";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "all_thirteen_distinct:1:0:construct";
  context = "all_thirteen_distinct:2:0:activate_captured_lifecycle";
  context = "all_thirteen_distinct:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "all_thirteen_distinct:4:0:core_query";
  {
    const auto value = subject.coreStatistics(peer, true);
  require(value && *value == std::array<std::int64_t, 4>{1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
  context = "all_thirteen_distinct:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "all_thirteen_distinct:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase6() {
  std::string_view context = "all_thirteen_negative";
  OverallStatistics subject({-1LL, -2LL, -3LL, -4LL, -5LL, -6LL, -7LL, -8LL, -9LL, -10LL, -11LL, -12LL, -13LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "all_thirteen_negative:1:0:construct";
  context = "all_thirteen_negative:2:0:activate_captured_lifecycle";
  context = "all_thirteen_negative:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == -1LL, context);
  require(value.uploaded == -2LL, context);
  require(value.uptime == -3LL, context);
  require(value.markTime == -4LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{-10LL, -11LL, -12LL, -13LL, -9LL, -8LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "all_thirteen_negative:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "all_thirteen_negative:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == -4LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, -4LL, -5LL, -6LL, -7LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase7() {
  std::string_view context = "legacy_aggregate_without_splits";
  OverallStatistics subject({1234LL, 5678LL, 90LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "legacy_aggregate_without_splits:1:0:construct";
  context = "legacy_aggregate_without_splits:2:0:activate_captured_lifecycle";
  context = "legacy_aggregate_without_splits:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 1234LL, context);
  require(value.uploaded == 5678LL, context);
  require(value.uptime == 90LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 13LL, context);
  require(value.averageUpload == 63LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "legacy_aggregate_without_splits:4:0:core_query";
  {
    const auto value = subject.coreStatistics(peer, true);
  require(value && *value == std::array<std::int64_t, 4>{0LL, 0LL, 0LL, 0LL}, context);
  }
  context = "legacy_aggregate_without_splits:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "legacy_aggregate_without_splits:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 1234LL, context);
  require(value.uploaded == 5678LL, context);
  require(value.uptime == 90LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 13LL, context);
  require(value.averageUpload == 63LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{1234LL, 5678LL, 90LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase8() {
  std::string_view context = "unknown_root_and_all_dropped";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "unknown_root_and_all_dropped:1:0:construct";
  context = "unknown_root_and_all_dropped:2:0:activate_captured_lifecycle";
  context = "unknown_root_and_all_dropped:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "unknown_root_and_all_dropped:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "unknown_root_and_all_dropped:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase9() {
  std::string_view context = "field_downloaded_missing";
  OverallStatistics subject({0LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_downloaded_missing:1:0:construct";
  context = "field_downloaded_missing:2:0:activate_captured_lifecycle";
  context = "field_downloaded_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_downloaded_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_downloaded_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase10() {
  std::string_view context = "field_downloaded_integer";
  OverallStatistics subject({0LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_downloaded_integer:1:0:construct";
  context = "field_downloaded_integer:2:0:activate_captured_lifecycle";
  context = "field_downloaded_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_downloaded_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_downloaded_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase11() {
  std::string_view context = "field_downloaded_bytes";
  OverallStatistics subject({0LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_downloaded_bytes:1:0:construct";
  context = "field_downloaded_bytes:2:0:activate_captured_lifecycle";
  context = "field_downloaded_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_downloaded_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_downloaded_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase12() {
  std::string_view context = "field_uploaded_missing";
  OverallStatistics subject({101LL, 0LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_uploaded_missing:1:0:construct";
  context = "field_uploaded_missing:2:0:activate_captured_lifecycle";
  context = "field_uploaded_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_uploaded_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_uploaded_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 0LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase13() {
  std::string_view context = "field_uploaded_integer";
  OverallStatistics subject({101LL, 0LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_uploaded_integer:1:0:construct";
  context = "field_uploaded_integer:2:0:activate_captured_lifecycle";
  context = "field_uploaded_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_uploaded_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_uploaded_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 0LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase14() {
  std::string_view context = "field_uploaded_bytes";
  OverallStatistics subject({101LL, 0LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_uploaded_bytes:1:0:construct";
  context = "field_uploaded_bytes:2:0:activate_captured_lifecycle";
  context = "field_uploaded_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_uploaded_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_uploaded_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 0LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase15() {
  std::string_view context = "field_uptime_missing";
  OverallStatistics subject({101LL, 202LL, 0LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_uptime_missing:1:0:construct";
  context = "field_uptime_missing:2:0:activate_captured_lifecycle";
  context = "field_uptime_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_uptime_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_uptime_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 0LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase16() {
  std::string_view context = "field_uptime_integer";
  OverallStatistics subject({101LL, 202LL, 0LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_uptime_integer:1:0:construct";
  context = "field_uptime_integer:2:0:activate_captured_lifecycle";
  context = "field_uptime_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_uptime_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_uptime_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 0LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase17() {
  std::string_view context = "field_uptime_bytes";
  OverallStatistics subject({101LL, 202LL, 0LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_uptime_bytes:1:0:construct";
  context = "field_uptime_bytes:2:0:activate_captured_lifecycle";
  context = "field_uptime_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_uptime_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_uptime_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 0LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase18() {
  std::string_view context = "field_mark_time_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 0LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_time_missing:1:0:construct";
  context = "field_mark_time_missing:2:0:activate_captured_lifecycle";
  context = "field_mark_time_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_time_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_time_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 0LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase19() {
  std::string_view context = "field_mark_time_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 0LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_time_integer:1:0:construct";
  context = "field_mark_time_integer:2:0:activate_captured_lifecycle";
  context = "field_mark_time_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_time_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_time_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 0LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase20() {
  std::string_view context = "field_mark_time_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 0LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_time_bytes:1:0:construct";
  context = "field_mark_time_bytes:2:0:activate_captured_lifecycle";
  context = "field_mark_time_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_time_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_time_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 0LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase21() {
  std::string_view context = "field_mark_downloaded_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 0LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_downloaded_missing:1:0:construct";
  context = "field_mark_downloaded_missing:2:0:activate_captured_lifecycle";
  context = "field_mark_downloaded_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_downloaded_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_downloaded_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 0LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase22() {
  std::string_view context = "field_mark_downloaded_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 0LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_downloaded_integer:1:0:construct";
  context = "field_mark_downloaded_integer:2:0:activate_captured_lifecycle";
  context = "field_mark_downloaded_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_downloaded_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_downloaded_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 0LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase23() {
  std::string_view context = "field_mark_downloaded_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 0LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_downloaded_bytes:1:0:construct";
  context = "field_mark_downloaded_bytes:2:0:activate_captured_lifecycle";
  context = "field_mark_downloaded_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_downloaded_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_downloaded_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 0LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase24() {
  std::string_view context = "field_mark_uploaded_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 0LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_uploaded_missing:1:0:construct";
  context = "field_mark_uploaded_missing:2:0:activate_captured_lifecycle";
  context = "field_mark_uploaded_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_uploaded_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_uploaded_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 0LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase25() {
  std::string_view context = "field_mark_uploaded_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 0LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_uploaded_integer:1:0:construct";
  context = "field_mark_uploaded_integer:2:0:activate_captured_lifecycle";
  context = "field_mark_uploaded_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_uploaded_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_uploaded_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 0LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase26() {
  std::string_view context = "field_mark_uploaded_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 0LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_uploaded_bytes:1:0:construct";
  context = "field_mark_uploaded_bytes:2:0:activate_captured_lifecycle";
  context = "field_mark_uploaded_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_uploaded_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_uploaded_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 0LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase27() {
  std::string_view context = "field_mark_uptime_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 0LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_uptime_missing:1:0:construct";
  context = "field_mark_uptime_missing:2:0:activate_captured_lifecycle";
  context = "field_mark_uptime_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_uptime_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_uptime_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 0LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase28() {
  std::string_view context = "field_mark_uptime_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 0LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_uptime_integer:1:0:construct";
  context = "field_mark_uptime_integer:2:0:activate_captured_lifecycle";
  context = "field_mark_uptime_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_uptime_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_uptime_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 0LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase29() {
  std::string_view context = "field_mark_uptime_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 0LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_mark_uptime_bytes:1:0:construct";
  context = "field_mark_uptime_bytes:2:0:activate_captured_lifecycle";
  context = "field_mark_uptime_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_mark_uptime_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_mark_uptime_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 0LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase30() {
  std::string_view context = "field_dht_down_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 0LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_dht_down_missing:1:0:construct";
  context = "field_dht_down_missing:2:0:activate_captured_lifecycle";
  context = "field_dht_down_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_dht_down_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_dht_down_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 0LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase31() {
  std::string_view context = "field_dht_down_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 0LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_dht_down_integer:1:0:construct";
  context = "field_dht_down_integer:2:0:activate_captured_lifecycle";
  context = "field_dht_down_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_dht_down_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_dht_down_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 0LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase32() {
  std::string_view context = "field_dht_down_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 0LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_dht_down_bytes:1:0:construct";
  context = "field_dht_down_bytes:2:0:activate_captured_lifecycle";
  context = "field_dht_down_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_dht_down_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_dht_down_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 0LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase33() {
  std::string_view context = "field_dht_up_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 0LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_dht_up_missing:1:0:construct";
  context = "field_dht_up_missing:2:0:activate_captured_lifecycle";
  context = "field_dht_up_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 0LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_dht_up_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_dht_up_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 0LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 0LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase34() {
  std::string_view context = "field_dht_up_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 0LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_dht_up_integer:1:0:construct";
  context = "field_dht_up_integer:2:0:activate_captured_lifecycle";
  context = "field_dht_up_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 0LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_dht_up_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_dht_up_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 0LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 0LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase35() {
  std::string_view context = "field_dht_up_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 0LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_dht_up_bytes:1:0:construct";
  context = "field_dht_up_bytes:2:0:activate_captured_lifecycle";
  context = "field_dht_up_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 0LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_dht_up_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_dht_up_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 0LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 0LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase36() {
  std::string_view context = "field_p_uploaded_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 0LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_p_uploaded_missing:1:0:construct";
  context = "field_p_uploaded_missing:2:0:activate_captured_lifecycle";
  context = "field_p_uploaded_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_p_uploaded_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_p_uploaded_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 0LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase37() {
  std::string_view context = "field_p_uploaded_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 0LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_p_uploaded_integer:1:0:construct";
  context = "field_p_uploaded_integer:2:0:activate_captured_lifecycle";
  context = "field_p_uploaded_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_p_uploaded_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_p_uploaded_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 0LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase38() {
  std::string_view context = "field_p_uploaded_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 0LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_p_uploaded_bytes:1:0:construct";
  context = "field_p_uploaded_bytes:2:0:activate_captured_lifecycle";
  context = "field_p_uploaded_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_p_uploaded_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_p_uploaded_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 0LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase39() {
  std::string_view context = "field_d_uploaded_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 0LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_d_uploaded_missing:1:0:construct";
  context = "field_d_uploaded_missing:2:0:activate_captured_lifecycle";
  context = "field_d_uploaded_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 0LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_d_uploaded_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_d_uploaded_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 0LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 0LL, 1212LL, 1313LL}, context);
  }
}

void javaCase40() {
  std::string_view context = "field_d_uploaded_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 0LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_d_uploaded_integer:1:0:construct";
  context = "field_d_uploaded_integer:2:0:activate_captured_lifecycle";
  context = "field_d_uploaded_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 0LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_d_uploaded_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_d_uploaded_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 0LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 0LL, 1212LL, 1313LL}, context);
  }
}

void javaCase41() {
  std::string_view context = "field_d_uploaded_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 0LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_d_uploaded_bytes:1:0:construct";
  context = "field_d_uploaded_bytes:2:0:activate_captured_lifecycle";
  context = "field_d_uploaded_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 0LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_d_uploaded_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_d_uploaded_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 0LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 0LL, 1212LL, 1313LL}, context);
  }
}

void javaCase42() {
  std::string_view context = "field_p_downloaded_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 0LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_p_downloaded_missing:1:0:construct";
  context = "field_p_downloaded_missing:2:0:activate_captured_lifecycle";
  context = "field_p_downloaded_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 0LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_p_downloaded_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_p_downloaded_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 0LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 0LL, 1313LL}, context);
  }
}

void javaCase43() {
  std::string_view context = "field_p_downloaded_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 0LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_p_downloaded_integer:1:0:construct";
  context = "field_p_downloaded_integer:2:0:activate_captured_lifecycle";
  context = "field_p_downloaded_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 0LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_p_downloaded_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_p_downloaded_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 0LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 0LL, 1313LL}, context);
  }
}

void javaCase44() {
  std::string_view context = "field_p_downloaded_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 0LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_p_downloaded_bytes:1:0:construct";
  context = "field_p_downloaded_bytes:2:0:activate_captured_lifecycle";
  context = "field_p_downloaded_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 0LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_p_downloaded_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_p_downloaded_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 0LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 0LL, 1313LL}, context);
  }
}

void javaCase45() {
  std::string_view context = "field_d_downloaded_missing";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_d_downloaded_missing:1:0:construct";
  context = "field_d_downloaded_missing:2:0:activate_captured_lifecycle";
  context = "field_d_downloaded_missing:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 0LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_d_downloaded_missing:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_d_downloaded_missing:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 0LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 0LL}, context);
  }
}

void javaCase46() {
  std::string_view context = "field_d_downloaded_integer";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_d_downloaded_integer:1:0:construct";
  context = "field_d_downloaded_integer:2:0:activate_captured_lifecycle";
  context = "field_d_downloaded_integer:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 0LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_d_downloaded_integer:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_d_downloaded_integer:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 0LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 0LL}, context);
  }
}

void javaCase47() {
  std::string_view context = "field_d_downloaded_bytes";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "field_d_downloaded_bytes:1:0:construct";
  context = "field_d_downloaded_bytes:2:0:activate_captured_lifecycle";
  context = "field_d_downloaded_bytes:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 0LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "field_d_downloaded_bytes:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "field_d_downloaded_bytes:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 404LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 0LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 0LL}, context);
  }
}

void javaCase48() {
  std::string_view context = "preexisting_gm_first_update";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {11LL, 13LL, 17LL, 19LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {11LL, 13LL, 17LL, 19LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "preexisting_gm_first_update:1:0:construct";
  context = "preexisting_gm_first_update:2:0:activate_captured_lifecycle";
  context = "preexisting_gm_first_update:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 19LL, 17LL, 13LL, 11LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "preexisting_gm_first_update:4:0:core_query";
  {
    const auto value = subject.coreStatistics(peer, true);
  require(value && *value == std::array<std::int64_t, 4>{19LL, 17LL, 13LL, 11LL}, context);
  }
  context = "preexisting_gm_first_update:5:0:set_clock";
  now = 1001000LL;
  context = "preexisting_gm_first_update:6:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "preexisting_gm_first_update:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 24LL, context);
  require(value.uploaded == 36LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{19LL, 17LL, 13LL, 11LL, 0LL, 0LL, 19LL, 17LL, 13LL, 11LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase49() {
  std::string_view context = "construction_two_clock_reads";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1002000LL);
  [[maybe_unused]] std::int64_t now = 1003000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "construction_two_clock_reads:1:0:construct";
  context = "construction_two_clock_reads:2:0:activate_captured_lifecycle";
  context = "construction_two_clock_reads:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "construction_two_clock_reads:4:0:query_session_uptime";
  require(subject.sessionUptime(now) == 3LL, context);
  context = "construction_two_clock_reads:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "construction_two_clock_reads:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 1LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase50() {
  std::string_view context = "supplied_product_filename";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "supplied_product_filename:1:0:construct";
  context = "supplied_product_filename:2:0:activate_captured_lifecycle";
  context = "supplied_product_filename:3:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "supplied_product_filename:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase51() {
  std::string_view context = "clock_delta_0_periodic";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_0_periodic:1:0:construct";
  context = "clock_delta_0_periodic:2:0:activate_captured_lifecycle";
  context = "clock_delta_0_periodic:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_0_periodic:4:0:set_clock";
  now = 1000000LL;
  context = "clock_delta_0_periodic:5:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_0_periodic:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase52() {
  std::string_view context = "clock_delta_0_force";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_0_force:1:0:construct";
  context = "clock_delta_0_force:2:0:activate_captured_lifecycle";
  context = "clock_delta_0_force:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_0_force:4:0:set_clock";
  now = 1000000LL;
  context = "clock_delta_0_force:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_0_force:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{84LL, 100LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL}, context);
  }
}

void javaCase53() {
  std::string_view context = "clock_delta_1_periodic";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_1_periodic:1:0:construct";
  context = "clock_delta_1_periodic:2:0:activate_captured_lifecycle";
  context = "clock_delta_1_periodic:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_1_periodic:4:0:set_clock";
  now = 1001000LL;
  context = "clock_delta_1_periodic:5:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_1_periodic:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase54() {
  std::string_view context = "clock_delta_1_force";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_1_force:1:0:construct";
  context = "clock_delta_1_force:2:0:activate_captured_lifecycle";
  context = "clock_delta_1_force:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_1_force:4:0:set_clock";
  now = 1001000LL;
  context = "clock_delta_1_force:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_1_force:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{84LL, 100LL, 1LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL}, context);
  }
}

void javaCase55() {
  std::string_view context = "clock_delta_99_periodic";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_99_periodic:1:0:construct";
  context = "clock_delta_99_periodic:2:0:activate_captured_lifecycle";
  context = "clock_delta_99_periodic:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_99_periodic:4:0:set_clock";
  now = 1099000LL;
  context = "clock_delta_99_periodic:5:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_99_periodic:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 99LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 1LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase56() {
  std::string_view context = "clock_delta_99_force";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_99_force:1:0:construct";
  context = "clock_delta_99_force:2:0:activate_captured_lifecycle";
  context = "clock_delta_99_force:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_99_force:4:0:set_clock";
  now = 1099000LL;
  context = "clock_delta_99_force:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_99_force:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 99LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 1LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{84LL, 100LL, 99LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL}, context);
  }
}

void javaCase57() {
  std::string_view context = "clock_delta_100_periodic";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_100_periodic:1:0:construct";
  context = "clock_delta_100_periodic:2:0:activate_captured_lifecycle";
  context = "clock_delta_100_periodic:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_100_periodic:4:0:set_clock";
  now = 1100000LL;
  context = "clock_delta_100_periodic:5:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_100_periodic:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 100LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 1LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase58() {
  std::string_view context = "clock_delta_100_force";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_100_force:1:0:construct";
  context = "clock_delta_100_force:2:0:activate_captured_lifecycle";
  context = "clock_delta_100_force:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_100_force:4:0:set_clock";
  now = 1100000LL;
  context = "clock_delta_100_force:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_100_force:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 100LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 1LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{84LL, 100LL, 100LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 53LL, 47LL, 43LL, 41LL}, context);
  }
}

void javaCase59() {
  std::string_view context = "clock_delta_101_periodic";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_101_periodic:1:0:construct";
  context = "clock_delta_101_periodic:2:0:activate_captured_lifecycle";
  context = "clock_delta_101_periodic:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_101_periodic:4:0:set_clock";
  now = 1101000LL;
  context = "clock_delta_101_periodic:5:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_101_periodic:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase60() {
  std::string_view context = "clock_delta_101_force";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clock_delta_101_force:1:0:construct";
  context = "clock_delta_101_force:2:0:activate_captured_lifecycle";
  context = "clock_delta_101_force:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "clock_delta_101_force:4:0:set_clock";
  now = 1101000LL;
  context = "clock_delta_101_force:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clock_delta_101_force:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase61() {
  std::string_view context = "backward_force_then_recovery";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "backward_force_then_recovery:1:0:construct";
  context = "backward_force_then_recovery:2:0:activate_captured_lifecycle";
  context = "backward_force_then_recovery:3:0:set_gm";
  peer = {5LL, 7LL, 11LL, 13LL};
  context = "backward_force_then_recovery:4:0:set_clock";
  now = 999000LL;
  context = "backward_force_then_recovery:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "backward_force_then_recovery:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "backward_force_then_recovery:7:0:set_clock";
  now = 1000000LL;
  context = "backward_force_then_recovery:8:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "backward_force_then_recovery:9:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 12LL, context);
  require(value.uploaded == 24LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{13LL, 11LL, 7LL, 5LL, 0LL, 0LL, 13LL, 11LL, 7LL, 5LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "backward_force_then_recovery:10:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "backward_force_then_recovery:11:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 12LL, context);
  require(value.uploaded == 24LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{13LL, 11LL, 7LL, 5LL, 0LL, 0LL, 13LL, 11LL, 7LL, 5LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{12LL, 24LL, 1LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 13LL, 11LL, 7LL, 5LL}, context);
  }
}

void javaCase62() {
  std::string_view context = "subsecond_clock_truncation";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "subsecond_clock_truncation:1:0:construct";
  context = "subsecond_clock_truncation:2:0:activate_captured_lifecycle";
  context = "subsecond_clock_truncation:3:0:set_gm";
  peer = {1LL, 2LL, 3LL, 4LL};
  context = "subsecond_clock_truncation:4:0:set_clock";
  now = 1000999LL;
  context = "subsecond_clock_truncation:5:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "subsecond_clock_truncation:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 3LL, context);
  require(value.uploaded == 7LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{4LL, 3LL, 2LL, 1LL, 0LL, 0LL, 4LL, 3LL, 2LL, 1LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "subsecond_clock_truncation:7:0:set_clock";
  now = 1001000LL;
  context = "subsecond_clock_truncation:8:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "subsecond_clock_truncation:9:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 3LL, context);
  require(value.uploaded == 7LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{4LL, 3LL, 2LL, 1LL, 0LL, 0LL, 4LL, 3LL, 2LL, 1LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase63() {
  std::string_view context = "forward_rejected_interval_retains_byte_baseline";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "forward_rejected_interval_retains_byte_baseline:1:0:construct";
  context = "forward_rejected_interval_retains_byte_baseline:2:0:activate_captured_lifecycle";
  context = "forward_rejected_interval_retains_byte_baseline:3:0:set_gm";
  peer = {100LL, 10LL, 200LL, 20LL};
  context = "forward_rejected_interval_retains_byte_baseline:4:0:set_clock";
  now = 1101000LL;
  context = "forward_rejected_interval_retains_byte_baseline:5:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "forward_rejected_interval_retains_byte_baseline:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 110LL, context);
  require(value.uploaded == 220LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "forward_rejected_interval_retains_byte_baseline:7:0:set_gm";
  peer = {101LL, 11LL, 201LL, 21LL};
  context = "forward_rejected_interval_retains_byte_baseline:8:0:set_clock";
  now = 1102000LL;
  context = "forward_rejected_interval_retains_byte_baseline:9:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "forward_rejected_interval_retains_byte_baseline:10:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 112LL, context);
  require(value.uploaded == 222LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{21LL, 201LL, 11LL, 101LL, 0LL, 0LL, 21LL, 201LL, 11LL, 101LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{112LL, 222LL, 1LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 21LL, 201LL, 11LL, 101LL}, context);
  }
}

void javaCase64() {
  std::string_view context = "zero_delta_tenth_tick_saves";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "zero_delta_tenth_tick_saves:1:0:construct";
  context = "zero_delta_tenth_tick_saves:2:0:activate_captured_lifecycle";
  context = "zero_delta_tenth_tick_saves:3:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:3:1:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:3:2:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:3:3:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:3:4:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:3:5:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:3:6:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:3:7:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:3:8:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "zero_delta_tenth_tick_saves:5:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
  context = "zero_delta_tenth_tick_saves:7:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "zero_delta_tenth_tick_saves:8:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 2, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves[1] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase65() {
  std::string_view context = "minute_ten_tick_save_then_force";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "minute_ten_tick_save_then_force:1:0:construct";
  context = "minute_ten_tick_save_then_force:2:0:activate_captured_lifecycle";
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:1:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:2:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:3:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:4:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:5:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:6:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:7:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:8:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  now += 60000LL; // Explicit fixture clock input only.
  context = "minute_ten_tick_save_then_force:3:9:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "minute_ten_tick_save_then_force:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 600LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 600LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
  context = "minute_ten_tick_save_then_force:5:0:set_clock";
  now = 1601000LL;
  context = "minute_ten_tick_save_then_force:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "minute_ten_tick_save_then_force:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 601LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 2, context);
  require(saves[0] == OverallValues{0LL, 0LL, 600LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves[1] == OverallValues{0LL, 0LL, 601LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase66() {
  std::string_view context = "monotonic_component_deltas";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "monotonic_component_deltas:1:0:construct";
  context = "monotonic_component_deltas:2:0:activate_captured_lifecycle";
  context = "monotonic_component_deltas:3:0:set_gm";
  peer = {10LL, 20LL, 30LL, 40LL};
  context = "monotonic_component_deltas:4:0:set_clock";
  now = 1001000LL;
  context = "monotonic_component_deltas:5:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "monotonic_component_deltas:6:0:set_gm";
  peer = {15LL, 23LL, 35LL, 47LL};
  context = "monotonic_component_deltas:7:0:set_clock";
  now = 1002000LL;
  context = "monotonic_component_deltas:8:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "monotonic_component_deltas:9:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 38LL, context);
  require(value.uploaded == 82LL, context);
  require(value.uptime == 2LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 19LL, context);
  require(value.averageUpload == 41LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{47LL, 35LL, 23LL, 15LL, 0LL, 0LL, 47LL, 35LL, 23LL, 15LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{38LL, 82LL, 2LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 47LL, 35LL, 23LL, 15LL}, context);
  }
}

void javaCase67() {
  std::string_view context = "provider_counter_reset_above_historical_zero";
  OverallStatistics subject({1000LL, 2000LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 1000LL, 1000LL, 1000LL, 1000LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "provider_counter_reset_above_historical_zero:1:0:construct";
  context = "provider_counter_reset_above_historical_zero:2:0:activate_captured_lifecycle";
  context = "provider_counter_reset_above_historical_zero:3:0:set_gm";
  peer = {100LL, 10LL, 200LL, 20LL};
  context = "provider_counter_reset_above_historical_zero:4:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "provider_counter_reset_above_historical_zero:5:0:set_gm";
  peer = {3LL, 2LL, 5LL, 4LL};
  context = "provider_counter_reset_above_historical_zero:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "provider_counter_reset_above_historical_zero:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 1005LL, context);
  require(value.uploaded == 2009LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1004LL, 1005LL, 1002LL, 1003LL, 0LL, 0LL, 4LL, 5LL, 2LL, 3LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{1005LL, 2009LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 1004LL, 1005LL, 1002LL, 1003LL}, context);
  }
}

void javaCase68() {
  std::string_view context = "negative_delta_clamps";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "negative_delta_clamps:1:0:construct";
  context = "negative_delta_clamps:2:0:activate_captured_lifecycle";
  context = "negative_delta_clamps:3:0:set_gm";
  peer = {-100LL, -20LL, -200LL, -30LL};
  context = "negative_delta_clamps:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "negative_delta_clamps:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, -30LL, -200LL, -20LL, -100LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase69() {
  std::string_view context = "combined_current_add_wrap";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "combined_current_add_wrap:1:0:construct";
  context = "combined_current_add_wrap:2:0:activate_captured_lifecycle";
  context = "combined_current_add_wrap:3:0:set_gm";
  peer = {9223372036854775807LL, 1LL, 9223372036854775807LL, 1LL};
  context = "combined_current_add_wrap:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "combined_current_add_wrap:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1LL, 9223372036854775807LL, 1LL, 9223372036854775807LL, 0LL, 0LL, 1LL, 9223372036854775807LL, 1LL, 9223372036854775807LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 1LL, 9223372036854775807LL, 1LL, 9223372036854775807LL}, context);
  }
}

void javaCase70() {
  std::string_view context = "historical_add_wrap_then_clamp";
  OverallStatistics subject({9223372036854775807LL, 9223372036854775807LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 9223372036854775807LL, 0LL, 9223372036854775807LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "historical_add_wrap_then_clamp:1:0:construct";
  context = "historical_add_wrap_then_clamp:2:0:activate_captured_lifecycle";
  context = "historical_add_wrap_then_clamp:3:0:set_gm";
  peer = {1LL, 0LL, 1LL, 0LL};
  context = "historical_add_wrap_then_clamp:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "historical_add_wrap_then_clamp:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 1LL, 0LL, 1LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase71() {
  std::string_view context = "uptime_add_wrap_not_postclamped";
  OverallStatistics subject({0LL, 0LL, 9223372036854775807LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "uptime_add_wrap_not_postclamped:1:0:construct";
  context = "uptime_add_wrap_not_postclamped:2:0:activate_captured_lifecycle";
  context = "uptime_add_wrap_not_postclamped:3:0:set_clock";
  now = 1001000LL;
  context = "uptime_add_wrap_not_postclamped:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "uptime_add_wrap_not_postclamped:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == (-9223372036854775807LL - 1), context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, (-9223372036854775807LL - 1), 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase72() {
  std::string_view context = "negative_uptime_reset_before_add";
  OverallStatistics subject({0LL, 0LL, (-9223372036854775807LL - 1), 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "negative_uptime_reset_before_add:1:0:construct";
  context = "negative_uptime_reset_before_add:2:0:activate_captured_lifecycle";
  context = "negative_uptime_reset_before_add:3:0:set_clock";
  now = 1001000LL;
  context = "negative_uptime_reset_before_add:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "negative_uptime_reset_before_add:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 1LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase73() {
  std::string_view context = "set_mark_does_not_sample_unsaved_gm";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "set_mark_does_not_sample_unsaved_gm:1:0:construct";
  context = "set_mark_does_not_sample_unsaved_gm:2:0:activate_captured_lifecycle";
  context = "set_mark_does_not_sample_unsaved_gm:3:0:set_gm";
  peer = {100LL, 10LL, 200LL, 20LL};
  context = "set_mark_does_not_sample_unsaved_gm:4:0:set_mark";
  subject.setMark(now);
  context = "set_mark_does_not_sample_unsaved_gm:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 1000000LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "set_mark_does_not_sample_unsaved_gm:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "set_mark_does_not_sample_unsaved_gm:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 110LL, context);
  require(value.uploaded == 220LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 1000000LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{20LL, 200LL, 10LL, 100LL, 0LL, 0LL, 20LL, 200LL, 10LL, 100LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{110LL, 220LL, 0LL, 1000000LL, 0LL, 0LL, 0LL, 0LL, 0LL, 20LL, 200LL, 10LL, 100LL}, context);
  }
  context = "set_mark_does_not_sample_unsaved_gm:8:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Downloaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 110LL}, context);
  }
  context = "set_mark_does_not_sample_unsaved_gm:9:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uploaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 220LL}, context);
  }
  context = "set_mark_does_not_sample_unsaved_gm:10:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uptime);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 0LL}, context);
  }
}

void javaCase74() {
  std::string_view context = "clear_mark_no_immediate_save";
  OverallStatistics subject({101LL, 202LL, 303LL, 404LL, 505LL, 606LL, 707LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "clear_mark_no_immediate_save:1:0:construct";
  context = "clear_mark_no_immediate_save:2:0:activate_captured_lifecycle";
  context = "clear_mark_no_immediate_save:3:0:set_mark";
  subject.setMark(now);
  context = "clear_mark_no_immediate_save:4:0:clear_mark";
  subject.clearMark();
  context = "clear_mark_no_immediate_save:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "clear_mark_no_immediate_save:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "clear_mark_no_immediate_save:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 101LL, context);
  require(value.uploaded == 202LL, context);
  require(value.uptime == 303LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{1010LL, 1111LL, 1212LL, 1313LL, 909LL, 808LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{101LL, 202LL, 303LL, 0LL, 0LL, 0LL, 0LL, 808LL, 909LL, 1010LL, 1111LL, 1212LL, 1313LL}, context);
  }
}

void javaCase75() {
  std::string_view context = "since_reads_clamp_persisted_high_marks";
  OverallStatistics subject({10LL, 20LL, 30LL, 0LL, 100LL, 200LL, 300LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "since_reads_clamp_persisted_high_marks:1:0:construct";
  context = "since_reads_clamp_persisted_high_marks:2:0:activate_captured_lifecycle";
  context = "since_reads_clamp_persisted_high_marks:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 10LL, context);
  require(value.uploaded == 20LL, context);
  require(value.uptime == 30LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "since_reads_clamp_persisted_high_marks:4:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Downloaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 0LL}, context);
  }
  context = "since_reads_clamp_persisted_high_marks:5:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uploaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 0LL}, context);
  }
  context = "since_reads_clamp_persisted_high_marks:6:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uptime);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 0LL}, context);
  }
  context = "since_reads_clamp_persisted_high_marks:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 10LL, context);
  require(value.uploaded == 20LL, context);
  require(value.uptime == 30LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "since_reads_clamp_persisted_high_marks:8:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "since_reads_clamp_persisted_high_marks:9:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 10LL, context);
  require(value.uploaded == 20LL, context);
  require(value.uptime == 30LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{10LL, 20LL, 30LL, 0LL, 10LL, 20LL, 30LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase76() {
  std::string_view context = "negative_mark_baselines";
  OverallStatistics subject({10LL, 20LL, 30LL, 0LL, -5LL, -7LL, -11LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "negative_mark_baselines:1:0:construct";
  context = "negative_mark_baselines:2:0:activate_captured_lifecycle";
  context = "negative_mark_baselines:3:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Downloaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 15LL}, context);
  }
  context = "negative_mark_baselines:4:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uploaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 27LL}, context);
  }
  context = "negative_mark_baselines:5:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uptime);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 41LL}, context);
  }
  context = "negative_mark_baselines:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "negative_mark_baselines:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 10LL, context);
  require(value.uploaded == 20LL, context);
  require(value.uptime == 30LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{10LL, 20LL, 30LL, 0LL, -5LL, -7LL, -11LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase77() {
  std::string_view context = "mark_timestamp_independent_of_since_values";
  OverallStatistics subject({19LL, 29LL, 39LL, 0LL, 3LL, 5LL, 7LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  [[maybe_unused]] OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "mark_timestamp_independent_of_since_values:1:0:construct";
  context = "mark_timestamp_independent_of_since_values:2:0:activate_captured_lifecycle";
  context = "mark_timestamp_independent_of_since_values:3:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Downloaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 16LL}, context);
  }
  context = "mark_timestamp_independent_of_since_values:4:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uploaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 24LL}, context);
  }
  context = "mark_timestamp_independent_of_since_values:5:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uptime);
  require(value && *value == OverallScalar{OverallScalarKind::Long, 32LL}, context);
  }
  context = "mark_timestamp_independent_of_since_values:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 19LL, context);
  require(value.uploaded == 29LL, context);
  require(value.uptime == 39LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase78() {
  std::string_view context = "since_subtraction_wrap";
  OverallStatistics subject({9223372036854775807LL, 9223372036854775807LL, 9223372036854775807LL, 0LL, -1LL, -1LL, -1LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  [[maybe_unused]] OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "since_subtraction_wrap:1:0:construct";
  context = "since_subtraction_wrap:2:0:activate_captured_lifecycle";
  context = "since_subtraction_wrap:3:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Downloaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, (-9223372036854775807LL - 1)}, context);
  }
  context = "since_subtraction_wrap:4:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uploaded);
  require(value && *value == OverallScalar{OverallScalarKind::Long, (-9223372036854775807LL - 1)}, context);
  }
  context = "since_subtraction_wrap:5:0:query_since";
  {
    auto value = subject.querySince(OverallSince::Uptime);
  require(value && *value == OverallScalar{OverallScalarKind::Long, (-9223372036854775807LL - 1)}, context);
  }
  context = "since_subtraction_wrap:6:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 9223372036854775807LL, context);
  require(value.uploaded == 9223372036854775807LL, context);
  require(value.uptime == 9223372036854775807LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 1LL, context);
  require(value.averageUpload == 1LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
}

void javaCase79() {
  std::string_view context = "average_uptime_0";
  OverallStatistics subject({4294967302LL, 8589934602LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  [[maybe_unused]] OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "average_uptime_0:1:0:construct";
  context = "average_uptime_0:2:0:activate_captured_lifecycle";
  context = "average_uptime_0:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 4294967302LL, context);
  require(value.uploaded == 8589934602LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "average_uptime_0:4:0:query_since";
  {
    auto value = subject.querySince(OverallSince::AverageDownload);
  require(value && *value == OverallScalar{OverallScalarKind::Integer, 0LL}, context);
  }
  context = "average_uptime_0:5:0:query_since";
  {
    auto value = subject.querySince(OverallSince::AverageUpload);
  require(value && *value == OverallScalar{OverallScalarKind::Integer, 0LL}, context);
  }
}

void javaCase80() {
  std::string_view context = "average_uptime_1";
  OverallStatistics subject({4294967302LL, 8589934602LL, 1LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  [[maybe_unused]] OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "average_uptime_1:1:0:construct";
  context = "average_uptime_1:2:0:activate_captured_lifecycle";
  context = "average_uptime_1:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 4294967302LL, context);
  require(value.uploaded == 8589934602LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "average_uptime_1:4:0:query_since";
  {
    auto value = subject.querySince(OverallSince::AverageDownload);
  require(value && *value == OverallScalar{OverallScalarKind::Integer, 0LL}, context);
  }
  context = "average_uptime_1:5:0:query_since";
  {
    auto value = subject.querySince(OverallSince::AverageUpload);
  require(value && *value == OverallScalar{OverallScalarKind::Integer, 0LL}, context);
  }
}

void javaCase81() {
  std::string_view context = "average_uptime_2";
  OverallStatistics subject({4294967302LL, 8589934602LL, 2LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  [[maybe_unused]] OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "average_uptime_2:1:0:construct";
  context = "average_uptime_2:2:0:activate_captured_lifecycle";
  context = "average_uptime_2:3:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 4294967302LL, context);
  require(value.uploaded == 8589934602LL, context);
  require(value.uptime == 2LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == -2147483645LL, context);
  require(value.averageUpload == 5LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "average_uptime_2:4:0:query_since";
  {
    auto value = subject.querySince(OverallSince::AverageDownload);
  require(value && *value == OverallScalar{OverallScalarKind::Integer, -2147483645LL}, context);
  }
  context = "average_uptime_2:5:0:query_since";
  {
    auto value = subject.querySince(OverallSince::AverageUpload);
  require(value && *value == OverallScalar{OverallScalarKind::Integer, 5LL}, context);
  }
}

void javaCase82() {
  std::string_view context = "session_wallclock_backward_and_wrap";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  [[maybe_unused]] OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "session_wallclock_backward_and_wrap:1:0:construct";
  context = "session_wallclock_backward_and_wrap:2:0:activate_captured_lifecycle";
  context = "session_wallclock_backward_and_wrap:3:0:set_clock";
  now = 999000LL;
  context = "session_wallclock_backward_and_wrap:4:0:query_session_uptime";
  require(subject.sessionUptime(now) == -1LL, context);
  context = "session_wallclock_backward_and_wrap:5:0:set_clock";
  now = (-9223372036854775807LL - 1);
  context = "session_wallclock_backward_and_wrap:6:0:query_session_uptime";
  require(subject.sessionUptime(now) == 9223372036853775LL, context);
  context = "session_wallclock_backward_and_wrap:7:0:set_clock";
  now = 9223372036854775807LL;
  context = "session_wallclock_backward_and_wrap:8:0:query_session_uptime";
  require(subject.sessionUptime(now) == 9223372036853775LL, context);
}

void javaCase83() {
  std::string_view context = "dht_two_transport_sum_and_separation";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Ready;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{17LL, 19LL}, {23LL, 29LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "dht_two_transport_sum_and_separation:1:0:construct";
  context = "dht_two_transport_sum_and_separation:2:0:activate_captured_lifecycle";
  context = "dht_two_transport_sum_and_separation:3:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_two_transport_sum_and_separation:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 40LL, 48LL, 0LL, 0LL, 0LL, 0LL, 40LL, 48LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 48LL, 40LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
  context = "dht_two_transport_sum_and_separation:5:0:set_dht_counters";
  cells = {{20LL, 22LL}, {26LL, 32LL}};
  context = "dht_two_transport_sum_and_separation:6:0:set_clock";
  now = 1001000LL;
  context = "dht_two_transport_sum_and_separation:7:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_two_transport_sum_and_separation:8:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 1LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 46LL, 54LL, 0LL, 0LL, 0LL, 0LL, 46LL, 54LL}, context);
  require(saves.size() == 2, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 48LL, 40LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves[1] == OverallValues{0LL, 0LL, 1LL, 0LL, 0LL, 0LL, 0LL, 54LL, 46LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase84() {
  std::string_view context = "dht_manager_not_initialized_retries";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Retry;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{17LL, 19LL}, {23LL, 29LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "dht_manager_not_initialized_retries:1:0:construct";
  context = "dht_manager_not_initialized_retries:2:0:activate_captured_lifecycle";
  context = "dht_manager_not_initialized_retries:3:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_manager_not_initialized_retries:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "dht_manager_not_initialized_retries:5:0:set_plugin";
  resolution = OverallDhtResolution::Ready;
  cells = {{17LL, 19LL}, {23LL, 29LL}};
  failed = std::nullopt;
  context = "dht_manager_not_initialized_retries:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_manager_not_initialized_retries:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 40LL, 48LL, 0LL, 0LL, 0LL, 0LL, 40LL, 48LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 48LL, 40LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase85() {
  std::string_view context = "dht_plugin_initializing_retries";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Retry;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{17LL, 19LL}, {23LL, 29LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "dht_plugin_initializing_retries:1:0:construct";
  context = "dht_plugin_initializing_retries:2:0:activate_captured_lifecycle";
  context = "dht_plugin_initializing_retries:3:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_plugin_initializing_retries:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "dht_plugin_initializing_retries:5:0:set_plugin";
  resolution = OverallDhtResolution::Ready;
  cells = {{17LL, 19LL}, {23LL, 29LL}};
  failed = std::nullopt;
  context = "dht_plugin_initializing_retries:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_plugin_initializing_retries:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 40LL, 48LL, 0LL, 0LL, 0LL, 0LL, 40LL, 48LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 48LL, 40LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase86() {
  std::string_view context = "dht_absent_cached_empty";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{17LL, 19LL}, {23LL, 29LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "dht_absent_cached_empty:1:0:construct";
  context = "dht_absent_cached_empty:2:0:activate_captured_lifecycle";
  context = "dht_absent_cached_empty:3:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_absent_cached_empty:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "dht_absent_cached_empty:5:0:set_plugin";
  resolution = OverallDhtResolution::Ready;
  cells = {{17LL, 19LL}, {23LL, 29LL}};
  failed = std::nullopt;
  context = "dht_absent_cached_empty:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_absent_cached_empty:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase87() {
  std::string_view context = "dht_disabled_cached_empty";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{17LL, 19LL}, {23LL, 29LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "dht_disabled_cached_empty:1:0:construct";
  context = "dht_disabled_cached_empty:2:0:activate_captured_lifecycle";
  context = "dht_disabled_cached_empty:3:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_disabled_cached_empty:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "dht_disabled_cached_empty:5:0:set_plugin";
  resolution = OverallDhtResolution::Ready;
  cells = {{17LL, 19LL}, {23LL, 29LL}};
  failed = std::nullopt;
  context = "dht_disabled_cached_empty:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_disabled_cached_empty:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase88() {
  std::string_view context = "dht_throw_discovery_cached_empty";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{17LL, 19LL}, {23LL, 29LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "dht_throw_discovery_cached_empty:1:0:construct";
  context = "dht_throw_discovery_cached_empty:2:0:activate_captured_lifecycle";
  context = "dht_throw_discovery_cached_empty:3:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_throw_discovery_cached_empty:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "dht_throw_discovery_cached_empty:5:0:set_plugin";
  resolution = OverallDhtResolution::Ready;
  cells = {{17LL, 19LL}, {23LL, 29LL}};
  failed = std::nullopt;
  context = "dht_throw_discovery_cached_empty:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_throw_discovery_cached_empty:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase89() {
  std::string_view context = "dht_throw_get_dhts_cached_empty";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{17LL, 19LL}, {23LL, 29LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "dht_throw_get_dhts_cached_empty:1:0:construct";
  context = "dht_throw_get_dhts_cached_empty:2:0:activate_captured_lifecycle";
  context = "dht_throw_get_dhts_cached_empty:3:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_throw_get_dhts_cached_empty:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "dht_throw_get_dhts_cached_empty:5:0:set_plugin";
  resolution = OverallDhtResolution::Ready;
  cells = {{17LL, 19LL}, {23LL, 29LL}};
  failed = std::nullopt;
  context = "dht_throw_get_dhts_cached_empty:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_throw_get_dhts_cached_empty:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase90() {
  std::string_view context = "dht_transport_throw_after_peer_updates";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Ready;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{17LL, 19LL}, {23LL, 29LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = 1;
  std::vector<OverallValues> saves;
  context = "dht_transport_throw_after_peer_updates:1:0:construct";
  context = "dht_transport_throw_after_peer_updates:2:0:activate_captured_lifecycle";
  context = "dht_transport_throw_after_peer_updates:3:0:set_gm";
  peer = {41LL, 43LL, 47LL, 53LL};
  context = "dht_transport_throw_after_peer_updates:4:0:tick";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, false);
  require(!result && result.error() == OverallError::TransportUnavailable, context);
  }
  context = "dht_transport_throw_after_peer_updates:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 0, context);
  }
  context = "dht_transport_throw_after_peer_updates:6:0:set_transport_failure";
  failed = std::nullopt;
  context = "dht_transport_throw_after_peer_updates:7:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_transport_throw_after_peer_updates:8:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 84LL, context);
  require(value.uploaded == 100LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{53LL, 47LL, 43LL, 41LL, 40LL, 48LL, 53LL, 47LL, 43LL, 41LL, 40LL, 48LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{84LL, 100LL, 0LL, 0LL, 0LL, 0LL, 0LL, 48LL, 40LL, 53LL, 47LL, 43LL, 41LL}, context);
  }
}

void javaCase91() {
  std::string_view context = "dht_sum_wrap_and_clamp";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Ready;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {{9223372036854775807LL, 9223372036854775807LL}, {1LL, 1LL}};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "dht_sum_wrap_and_clamp:1:0:construct";
  context = "dht_sum_wrap_and_clamp:2:0:activate_captured_lifecycle";
  context = "dht_sum_wrap_and_clamp:3:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "dht_sum_wrap_and_clamp:4:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, (-9223372036854775807LL - 1), (-9223372036854775807LL - 1)}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase92() {
  std::string_view context = "core_provider_started_gate_and_unflushed_delta";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "core_provider_started_gate_and_unflushed_delta:1:0:construct";
  context = "core_provider_started_gate_and_unflushed_delta:2:0:activate_captured_lifecycle";
  context = "core_provider_started_gate_and_unflushed_delta:3:0:set_gm";
  peer = {11LL, 13LL, 17LL, 19LL};
  context = "core_provider_started_gate_and_unflushed_delta:4:0:core_query";
  {
    const auto value = subject.coreStatistics(peer, false);
  require(!value, context);
  }
  context = "core_provider_started_gate_and_unflushed_delta:5:0:core_query";
  {
    const auto value = subject.coreStatistics(peer, true);
  require(value && *value == std::array<std::int64_t, 4>{19LL, 17LL, 13LL, 11LL}, context);
  }
  context = "core_provider_started_gate_and_unflushed_delta:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "core_provider_started_gate_and_unflushed_delta:7:0:set_gm";
  peer = {12LL, 15LL, 20LL, 23LL};
  context = "core_provider_started_gate_and_unflushed_delta:8:0:core_query";
  {
    const auto value = subject.coreStatistics(peer, true);
  require(value && *value == std::array<std::int64_t, 4>{23LL, 20LL, 15LL, 12LL}, context);
  }
  context = "core_provider_started_gate_and_unflushed_delta:9:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 24LL, context);
  require(value.uploaded == 36LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{19LL, 17LL, 13LL, 11LL, 0LL, 0LL, 19LL, 17LL, 13LL, 11LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{24LL, 36LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 19LL, 17LL, 13LL, 11LL}, context);
  }
}

void javaCase93() {
  std::string_view context = "force_close_repeated_equal_time";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "force_close_repeated_equal_time:1:0:construct";
  context = "force_close_repeated_equal_time:2:0:activate_captured_lifecycle";
  context = "force_close_repeated_equal_time:3:0:set_gm";
  peer = {1LL, 2LL, 3LL, 4LL};
  context = "force_close_repeated_equal_time:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "force_close_repeated_equal_time:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 3LL, context);
  require(value.uploaded == 7LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{4LL, 3LL, 2LL, 1LL, 0LL, 0LL, 4LL, 3LL, 2LL, 1LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{3LL, 7LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 4LL, 3LL, 2LL, 1LL}, context);
  }
  context = "force_close_repeated_equal_time:6:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "force_close_repeated_equal_time:7:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 3LL, context);
  require(value.uploaded == 7LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{4LL, 3LL, 2LL, 1LL, 0LL, 0LL, 4LL, 3LL, 2LL, 1LL, 0LL, 0LL}, context);
  require(saves.size() == 2, context);
  require(saves[0] == OverallValues{3LL, 7LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 4LL, 3LL, 2LL, 1LL}, context);
  require(saves[1] == OverallValues{3LL, 7LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 4LL, 3LL, 2LL, 1LL}, context);
  }
}

void javaCase94() {
  std::string_view context = "void_save_provider_silent_drop";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "void_save_provider_silent_drop:1:0:construct";
  context = "void_save_provider_silent_drop:2:0:activate_captured_lifecycle";
  context = "void_save_provider_silent_drop:3:0:set_save_behavior";
  context = "void_save_provider_silent_drop:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "void_save_provider_silent_drop:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaCase95() {
  std::string_view context = "void_save_provider_throw_releases_monitor";
  OverallStatistics subject({0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, 1000000LL, 1000000LL);
  [[maybe_unused]] std::int64_t now = 1000000LL;
  [[maybe_unused]] OverallPeerCounters peer = {0LL, 0LL, 0LL, 0LL};
  OverallDhtResolution resolution = OverallDhtResolution::Empty;
  [[maybe_unused]] std::vector<OverallTransportCounters> cells = {};
  [[maybe_unused]] std::optional<std::size_t> failed = std::nullopt;
  std::vector<OverallValues> saves;
  context = "void_save_provider_throw_releases_monitor:1:0:construct";
  context = "void_save_provider_throw_releases_monitor:2:0:activate_captured_lifecycle";
  context = "void_save_provider_throw_releases_monitor:3:0:set_save_behavior";
  context = "void_save_provider_throw_releases_monitor:4:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
    // Original provider throws AFTER capturing this save intention; domain state is retained.
  require(result->save.has_value(), context);
  }
  context = "void_save_provider_throw_releases_monitor:5:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 1, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
  context = "void_save_provider_throw_releases_monitor:6:0:set_save_behavior";
  context = "void_save_provider_throw_releases_monitor:7:0:force_close";
  {
    std::vector<std::optional<OverallTransportCounters>> sampled;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      sampled.push_back(failed && *failed == i ? std::nullopt : std::optional{cells[i]});
    }
    auto result = subject.update(peer, {resolution, sampled}, now, true);
  require(result.has_value(), context);
    if (result->save) { saves.push_back(*result->save); }
  }
  context = "void_save_provider_throw_releases_monitor:8:0:observe";
  {
    const auto value = subject.observe();
  require(value.downloaded == 0LL, context);
  require(value.uploaded == 0LL, context);
  require(value.uptime == 0LL, context);
  require(value.markTime == 0LL, context);
  require(value.averageDownload == 0LL, context);
  require(value.averageUpload == 0LL, context);
  require(value.snapshot == std::array<std::int64_t, 12>{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves.size() == 2, context);
  require(saves[0] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  require(saves[1] == OverallValues{0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, context);
  }
}

void javaLiteralCases() {
  javaCase1();
  javaCase2();
  javaCase3();
  javaCase4();
  javaCase5();
  javaCase6();
  javaCase7();
  javaCase8();
  javaCase9();
  javaCase10();
  javaCase11();
  javaCase12();
  javaCase13();
  javaCase14();
  javaCase15();
  javaCase16();
  javaCase17();
  javaCase18();
  javaCase19();
  javaCase20();
  javaCase21();
  javaCase22();
  javaCase23();
  javaCase24();
  javaCase25();
  javaCase26();
  javaCase27();
  javaCase28();
  javaCase29();
  javaCase30();
  javaCase31();
  javaCase32();
  javaCase33();
  javaCase34();
  javaCase35();
  javaCase36();
  javaCase37();
  javaCase38();
  javaCase39();
  javaCase40();
  javaCase41();
  javaCase42();
  javaCase43();
  javaCase44();
  javaCase45();
  javaCase46();
  javaCase47();
  javaCase48();
  javaCase49();
  javaCase50();
  javaCase51();
  javaCase52();
  javaCase53();
  javaCase54();
  javaCase55();
  javaCase56();
  javaCase57();
  javaCase58();
  javaCase59();
  javaCase60();
  javaCase61();
  javaCase62();
  javaCase63();
  javaCase64();
  javaCase65();
  javaCase66();
  javaCase67();
  javaCase68();
  javaCase69();
  javaCase70();
  javaCase71();
  javaCase72();
  javaCase73();
  javaCase74();
  javaCase75();
  javaCase76();
  javaCase77();
  javaCase78();
  javaCase79();
  javaCase80();
  javaCase81();
  javaCase82();
  javaCase83();
  javaCase84();
  javaCase85();
  javaCase86();
  javaCase87();
  javaCase88();
  javaCase89();
  javaCase90();
  javaCase91();
  javaCase92();
  javaCase93();
  javaCase94();
  javaCase95();
}
