#include "biglybtc/queue_file_priority.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void need(bool good, const char* why) { if (!good) { throw std::runtime_error(why); } }
template<class Integer> Integer number(std::string_view text) {
  Integer out{}; const auto value = std::from_chars(text.data(), text.data() + text.size(), out);
  need(value.ec == std::errc{} && value.ptr == text.data() + text.size(), "Invalid actual numeric observation"); return out;
}
bool boolean(std::string_view text) { need(text == "true" || text == "false", "Invalid actual Boolean observation"); return text == "true"; }
std::vector<std::string> fields(const std::string& line) {
  std::vector<std::string> out; std::size_t begin = 0;
  for (;;) {
    const auto end = line.find('\t', begin); out.push_back(line.substr(begin, end - begin));
    if (end == std::string::npos) { return out; } begin = end + 1;
  }
}
} // namespace
int main(int argc, char** argv) {
  try {
    need(argc == 2, "Usage: queue_file_priority_replay ACTUAL_JAVA_STDOUT");
    std::ifstream input(argv[1]); need(input.good(), "Actual original trace unavailable");
    biglybtc::compat::OriginalLayout layout{};
    std::vector<biglybtc::compat::FilePolicy> policies;
    std::vector<std::uint8_t> pieces;
    std::string phase, line; bool compared = true, stopped = false; std::size_t count = 0, bytes = 0;
    while (std::getline(input, line)) {
      bytes += line.size(); need(bytes <= 512 * 1024, "Actual fixture trace exceeded bound");
      const auto row = fields(line);
      if (row[0] == "QGEOMETRY") {
        need(row.size() == 5 && compared && count < 2 && row[1] == (count == 0 ? "loaded" : "edited"), "Invalid actual phase");
        phase = row[1]; compared = false; layout = {}; policies.clear(); pieces.clear();
        layout.pieceLength = number<std::int64_t>(row[2]); layout.bytes = number<std::int64_t>(row[3]);
        need(row[4].size() <= 1024, "Unbounded actual piece snapshot");
        for (char value : row[4]) { need(value == '0' || value == '1', "Invalid actual verified bit"); pieces.push_back(value - '0'); }
      } else if (row[0] == "QFILE") {
        need(row.size() == 9 && !compared && row[1] == phase && policies.size() < 32
            && number<std::size_t>(row[2]) == policies.size(), "Actual source index changed");
        const auto length = number<std::int64_t>(row[4]), downloaded = number<std::int64_t>(row[8]);
        need(downloaded >= 0 && downloaded <= length, "Invalid actual original downloaded bytes");
        layout.files.push_back({row[2], number<std::int64_t>(row[3]), length, boolean(row[7])});
        policies.push_back({boolean(row[6]), number<std::int32_t>(row[5])});
      } else if (row[0] == "QSTATS") {
        need(row.size() == 6 && !compared && row[1] == phase, "Invalid actual original stats frame");
        const auto actual = biglybtc::core::queueFilePriorityStatistics(layout, policies, pieces);
        need(actual.has_value(), "Native observer rejected actual original metadata/pieces");
        for (std::size_t i = 0; i < 4; ++i) { need((*actual)[i] == number<std::int32_t>(row[i + 2]), "Native differs from actual original file-priority statistic"); }
        compared = true; ++count; std::cout << "PASS\t" << phase << '\n';
      } else if (line == "QUEUE_FILE_PRIORITY_ORIGINAL_TRACE_STOPPED") { stopped = true; }
    }
    need(input.eof() && compared && stopped && count == 2, "Incomplete actual original trace");
    std::cout << "PASS actual original file-priority observation replay\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
