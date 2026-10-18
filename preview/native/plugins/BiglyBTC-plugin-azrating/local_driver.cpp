// SPDX-License-Identifier: GPL-2.0-only
#include "local_rating.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
namespace rating = biglybtc::plugins::rating;
rating::Text text(const std::string& token) {
  if (token == "-") { return {}; }
  if (token.empty() || token.front() != 'u' || token.size() % 4 != 1) { throw std::runtime_error("Invalid fixture token"); }
  std::u16string result;
  for (std::size_t i = 1; i < token.size(); i += 4) { result += static_cast<char16_t>(std::stoul(token.substr(i, 4), nullptr, 16)); }
  return result;
}
std::string text(const rating::Text& value) {
  if (!value) { return "-"; }
  std::ostringstream out; out << 'u' << std::hex << std::setfill('0');
  for (const auto unit : *value) { out << std::setw(4) << static_cast<unsigned>(unit); }
  return out.str();
}
int main(int argc, char** argv) {
  if (argc != 2 || !std::filesystem::canonical(argv[1]).string().starts_with("/tmp/biglybtc-")) { return 2; }
  std::ifstream input(argv[1]); if (!input) { return 3; }
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream in(line); char kind; std::string r,c,n; in >> kind >> r >> c >> n;
    const rating::LocalRatingInputs values{text(r),text(c),text(n)};
    if (kind == 'L') {
      const auto result = rating::loadLocalRating(values); if (!result) { return 4; }
      std::cout << "L " << result->value.score << ' ' << text(result->value.nick) << ' '
          << text(result->value.comment) << ' ' << result->invalidScoreText << '\n';
    } else if (kind == 'S') {
      std::int32_t score; std::string nick,comment; in >> score >> nick >> comment;
      const auto result = rating::planLocalRatingStore(values,rating::RatingData::create(score,text(nick),text(comment)));
      if (!result) { return 4; }
      std::cout << "S " << result->previous.invalidScoreText << ' ' << (result->failure ? "NULL_NICK" : "NONE") << ' '
          << result->writes.size() + static_cast<unsigned>(result->publishUrgently);
      for (const auto& write : result->writes) {
        std::cout << ' ' << (write.attribute == rating::LocalAttribute::Rating ? "R:" : "C:") << text(write.value);
      }
      if (result->publishUrgently) { std::cout << " P:1"; }
      std::cout << '\n';
    } else { return 5; }
    if (!in) { return 6; }
  }
}
