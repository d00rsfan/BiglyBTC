#include "bencode.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using biglybtc::probe::Document;
using biglybtc::probe::Limits;

namespace {
std::size_t checks = 0;
void check(bool condition) {
  ++checks;
  if (!condition) {
    throw std::runtime_error("Format check failed at check " + std::to_string(checks));
  }
}
}

int main() {
  try {
    for (const std::string valid : {"de", "le", "i0e", "i-1e", "0:", "3:abc",
        "i9223372036854775807e", "i-9223372036854775808e", "d1:zi1e1:ai2ee",
        "d1:ald1:bi42ee0:leee"}) {
      auto document = Document::parse(valid);
      check(document.has_value());
      check(document->serialize() == valid);
      check(biglybtc::core::encodeValue(document->root()) == valid);
    }
    const std::vector<std::string> invalid{"", "d", "l", "i", "e", "i01e", "i-0e",
        "i+1e", "ie", "i-01e", "i9223372036854775808e", "i-9223372036854775809e",
        "03:abc", "1x:a", "5:abc", "99999999999999999999999999999:a",
        "d1:ai1e1:ai2ee", "di1ei2ee", "d1:ae", "degarbage", "d1:ai1e"};
    for (const auto& input : invalid) {
      check(!Document::parse(input));
    }
    check(!Document::parse("4:abcd", Limits{.stringBytes = 3}));
    check(!Document::parse("4:abcd", Limits{.inputBytes = 5}));
    check(!Document::parse("li1ei2ee", Limits{.nodes = 2}));
    check(!Document::parse("lllee e", Limits{.depth = 1}));
    check(Document::parse("le", Limits{.depth = 0}).has_value());
    const std::string bytes = std::string("d1:x4:") + std::string("\0\xff\x80\x7f", 4) + "e";
    auto binary = Document::parse(bytes);
    check(binary && binary->serialize() == bytes);
    // Short strings exercise SSO moves; views must survive document copy/move
    // and destruction of the original argument/document, including binary data.
    auto retained = [] {
      auto original = Document::parse(std::string("d1:x3:abce"));
      check(original.has_value());
      auto copied = *original;
      return copied;
    }();
    check(retained.find("x") && retained.find("x")->bytes == "abc");
    auto moved = std::move(retained);
    check(moved.find("x")->bytes == "abc" && moved.serialize() == "d1:x3:abce");
    auto binaryCopy = *binary;
    binary = std::unexpected(biglybtc::core::ParseError{"released_for_test", 0});
    check(binaryCopy.find("x")->bytes == std::string_view("\0\xff\x80\x7f", 4));
    const std::string config = "d7:unknownld1:x3:rawe4:datae5:knowni1ee";
    auto edited = Document::parse(config);
    check(edited.has_value());
    auto uneditedCopy = *edited;
    check(edited->find("unknown") != nullptr);
    check(!edited->setInteger("missing", 2));
    check(!edited->setInteger("unknown", 2));
    check(edited->serialize() == config);
    check(edited->setInteger("known", std::numeric_limits<std::int64_t>::min()));
    check(edited->serialize() == "d7:unknownld1:x3:rawe4:datae5:knowni-9223372036854775808ee");
    check(Document::parse(edited->serialize()).has_value());
    check(uneditedCopy.serialize() == config && uneditedCopy.find("known")->integer == 1);
    // Fixed deterministic malformed-input stress; this is not a fuzzing campaign.
    std::uint32_t state = 0x42475443;
    for (int trial = 0; trial < 20000; ++trial) {
      state = state * 1664525U + 1013904223U;
      std::string input(state % 256, '\0');
      for (auto& byte : input) {
        state = state * 1664525U + 1013904223U;
        byte = static_cast<char>(state >> 24);
      }
      const auto decoded = Document::parse(input, Limits{.inputBytes = 256, .stringBytes = 256, .nodes = 128, .depth = 16});
      if (decoded) {
        check(decoded->serialize() == input);
        check(biglybtc::core::encodeValue(decoded->root()) == input);
      }
    }
    std::cout << "PASS " << checks << " checks; 20,000 malformed-input trials\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
