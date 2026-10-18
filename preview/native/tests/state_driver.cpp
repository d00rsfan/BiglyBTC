#include "encoded_document.hpp"
#include "profile.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  try {
    if (argc != 4 || std::filesystem::exists(argv[3])) { return 1; }
    if (std::filesystem::file_size(argv[2]) > biglybtc::storage::compatibleConfigLimits().inputBytes) { return 2; }
    std::ifstream input(argv[2], std::ios::binary);
    if (!input) { return 2; }
    auto envelope = biglybtc::storage::EncodedDocument::parse(
        std::string(std::istreambuf_iterator<char>(input), {}), biglybtc::storage::compatibleConfigLimits());
    if (!envelope) { return 2; }
    const std::string_view command(argv[1]);
    if (command == "set-port") {
      if (!envelope->setInteger("TCP.Listen.Port", 51414)) { return 2; }
    } else if (command == "invalidate-resume") {
      const auto* resumeValue = envelope->document().find("resume");
      if (!resumeValue || !resumeValue->find("data")) { return 2; }
      auto resumeBytes = biglybtc::core::encodeValue(*resumeValue, biglybtc::storage::compatibleConfigLimits());
      auto dataBytes = biglybtc::core::encodeValue(*resumeValue->find("data"), biglybtc::storage::compatibleConfigLimits());
      if (!resumeBytes || !dataBytes) { return 2; }
      auto resume = biglybtc::core::Document::parse(std::move(*resumeBytes), biglybtc::storage::compatibleConfigLimits());
      auto data = biglybtc::core::Document::parse(std::move(*dataBytes), biglybtc::storage::compatibleConfigLimits());
      if (!resume || !data || !data->setInteger("valid", 0)
          || !resume->replaceValue("data", data->serialize())
          || !envelope->replaceValue("resume", resume->serialize())) { return 2; }
    } else if (command != "roundtrip") {
      return 1;
    }
    const auto encoded = envelope->serialize();
    if (!encoded) { return 2; }
    std::ofstream output(argv[3], std::ios::binary);
    output.write(encoded->data(), static_cast<std::streamsize>(encoded->size()));
    return output ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
