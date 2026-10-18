// SPDX-License-Identifier: GPL-2.0-only
#include "rating_data.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace rating = biglybtc::plugins::rating;
std::string hex(std::string_view text) {
  std::ostringstream output; output << 'x' << std::hex << std::setfill('0');
  for (const unsigned char byte : text) { output << std::setw(2) << static_cast<unsigned>(byte); }
  return output.str();
}
std::string text(const rating::Text& value) {
  if (!value) { return "-"; }
  std::ostringstream output; output << 'x' << std::hex << std::setfill('0');
  for (const auto unit : *value) { output << std::setw(4) << static_cast<unsigned>(unit); }
  return output.str();
}
unsigned number(const std::string& value, std::size_t position, unsigned length) {
  return static_cast<unsigned>(std::stoul(value.substr(position, length), nullptr, 16));
}
rating::Text string(const std::string& value) {
  if (value == "-") { return std::nullopt; }
  if (value.empty() || value[0] != 'x' || value.size() % 4 != 1) { throw std::runtime_error("Invalid text token"); }
  std::u16string result;
  for (std::size_t i = 1; i < value.size(); i += 4) { result += static_cast<char16_t>(number(value, i, 4)); }
  return result;
}
std::string bytes(const std::string& value) {
  if (value.empty() || value[0] != 'x' || value.size() % 2 != 1) { throw std::runtime_error("Invalid byte token"); }
  std::string result;
  for (std::size_t i = 1; i < value.size(); i += 2) { result += static_cast<char>(number(value, i, 2)); }
  return result;
}
rating::RatingData data(std::istream& input) {
  std::int32_t score = 0; std::string nick, comment; input >> score >> nick >> comment;
  if (!input) { throw std::runtime_error("Invalid rating command"); }
  return rating::RatingData::create(score, string(nick), string(comment));
}
void show(const rating::RatingData& value) {
  const auto encoded = value.encode();
  std::cout << "DATA " << value.score << ' ' << text(value.nick) << ' ' << text(value.comment)
      << ' ' << value.needsPublishing() << ' ' << hex(encoded ? *encoded : "") << '\n';
}
void show(const rating::RatingResults& value, std::string_view status) {
  std::cout << "RESULT " << status << ' ' << std::bit_cast<std::uint32_t>(value.average()) << ' '
      << value.count() << ' ' << value.commentCount() << ' ' << value.roundedAverage() << ' '
      << value.averageText() << ' ' << (value.retained() ? static_cast<int>(value.retained()->size()) : -1)
      << ' ' << text(value.comments()) << '\n';
}
int main(int argc, char** argv) {
  if (argc != 2) { return 2; }
  const auto path = std::filesystem::canonical(argv[1]);
  if (!path.native().starts_with("/tmp/biglybtc-") || std::filesystem::file_size(path) > 16 * 1024 * 1024) { return 2; }
  std::ifstream stream(path);
  if (!stream.good()) { return 3; }
  rating::RatingResults result;
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream input(line); char command = 0; input >> command;
    if (command == 'C') { show(data(input)); }
    else if (command == 'D') {
      std::string raw; input >> raw;
      const auto original = bytes(raw);
      auto decoded = rating::decode(original);
      if (!decoded) { std::cout << "REJECT\n"; continue; }
      if (decoded->document.serialize() != original) { return 4; }
      show(decoded->value);
    } else if (command == 'K') {
      std::string raw; input >> raw;
      const auto hash = raw == "-" ? std::string{} : bytes(raw);
      const auto key = rating::lookupKey({reinterpret_cast<const std::uint8_t*>(hash.data()), hash.size()});
      if (!key) { std::cout << "KEY REJECT\n"; }
      else { std::cout << "KEY " << hex({reinterpret_cast<const char*>(key->data()), key->size()}) << '\n'; }
    } else if (command == 'R') { result = {}; show(result, "OK"); }
    else if (command == 'S') {
      std::uint32_t bits = 0; input >> bits; result.setAverage(std::bit_cast<float>(bits)); show(result, "OK");
    } else if (command == 'A') {
      int retain = 0; input >> retain;
      const auto status = result.add(data(input), retain != 0);
      show(result, status ? "OK" : status.error() == rating::Error::NullComment ? "NULL_COMMENT" : "LIMIT");
    } else { return 5; }
  }
}
