#include "biglybtc/queue_rate_providers.hpp"

#include <charconv>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace biglybtc::core;
namespace {
std::int64_t number(const std::string& text) {
  std::int64_t value = 0; const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if (r.ec != std::errc{} || r.ptr != text.data() + text.size() || value < 0) { throw std::runtime_error("Invalid literal primitive"); }
  return value;
}
}
int main() {
  try {
    QueueGlobalReceiveAverage global; QueueSmoothedEta eta;
    std::uint64_t sequence = 0; bool baseline = false; std::size_t observations = 0;
    for (std::string line; std::getline(std::cin, line);) {
      if (line.empty() || line.front() == '#') { continue; }
      std::istringstream input(line); std::vector<std::string> row;
      for (std::string field; std::getline(input, field, '\t');) { row.push_back(field); }
      if (row[0] == "RESET" && row.size() == 2) { global = {}; eta = {}; sequence = 0; baseline = false; continue; }
      if (row[0] == "G" && row.size() == 5) {
        const auto data = number(row[3]), protocol = number(row[4]);
        if (protocol > std::numeric_limits<std::int64_t>::max() - data) { throw std::runtime_error("Unadmitted combined overflow"); }
        const auto result = global.observe({{1, ++sequence, number(row[2])}, QueueRateScope::OriginalCombined, data + protocol});
        if (!baseline) {
          if (result) { throw std::runtime_error("Unexpected numeric baseline"); } baseline = true; continue;
        }
        if (!result) { throw std::runtime_error("Admitted global trace became unavailable"); }
        std::cout << "OBS\t" << row[1] << "\tkib\t" << *result << '\n'; ++observations;
      } else if (row[0] == "E" && row.size() == 10) {
        const QueueEtaSample sample{{1, ++sequence, number(row[2])}, number(row[3]),
            number(row[4]), number(row[5]), number(row[6]), number(row[7]), number(row[8])};
        // Final field is a source classification label, not numeric expected output.
        if (row[9] != "admitted") { throw std::runtime_error("Unadmitted ETA trace"); }
        const auto result = eta.observe(sample);
        if (!result) { throw std::runtime_error("Admitted ETA trace became unavailable"); }
        std::cout << "OBS\t" << row[1] << "\teta\t" << *result << '\n'; ++observations;
      } else { throw std::runtime_error("Invalid literal operation"); }
    }
    std::cout << "PASS_QUEUE_RATE_NATIVE\t" << observations << '\n'; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
