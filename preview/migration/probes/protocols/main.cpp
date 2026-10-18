#include "bencode.hpp"
#include "crypto.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {

std::string read(const std::filesystem::path& path) {
  const auto size = std::filesystem::file_size(path);
  if (size > 4 * 1024 * 1024) {
    throw std::runtime_error("Input exceeds probe limit");
  }
  std::ifstream stream(path, std::ios::binary);
  std::string result(static_cast<std::size_t>(size), '\0');
  if (!stream.read(result.data(), static_cast<std::streamsize>(result.size()))) {
    throw std::runtime_error("Unable to read input");
  }
  return result;
}

void writeNew(const char* name, std::string_view value) {
  const int fd = ::open(name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) {
    throw std::runtime_error("Output must be a new file");
  }
  std::size_t offset = 0;
  bool failed = false;
  while (offset < value.size()) {
    const auto count = ::write(fd, value.data() + offset, value.size() - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      failed = true;
      break;
    }
    offset += static_cast<std::size_t>(count);
  }
  const bool syncFailed = ::fsync(fd) != 0;
  const bool closeFailed = ::close(fd) != 0;
  if (failed || syncFailed || closeFailed) {
    ::unlink(name);
    throw std::runtime_error("Unable to write complete output");
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 6 && std::string_view(argv[1]) == "verify" && std::string_view(argv[5]) == "--legacy") {
      return biglybtc::probe::verifyLegacySignature(read(argv[2]), read(argv[3]), read(argv[4])) ? 0 : 2;
    }
    if (argc == 6 && std::string_view(argv[1]) == "sign") {
      writeNew(argv[5], biglybtc::probe::signLegacyMessage(read(argv[2]), read(argv[3]), read(argv[4])));
      return 0;
    }
    if ((argc == 4 && std::string_view(argv[1]) == "roundtrip")
        || (argc == 6 && std::string_view(argv[1]) == "set-integer")) {
      auto document = biglybtc::probe::Document::parse(read(argv[2]));
      if (!document) {
        std::cerr << document.error().code << " at " << document.error().offset << '\n';
        return 2;
      }
      if (argc == 6) {
        std::int64_t value = 0;
        const std::string_view number(argv[4]);
        const auto result = std::from_chars(number.data(), number.data() + number.size(), value);
        if (result.ec != std::errc{} || result.ptr != number.data() + number.size()
            || !document->setInteger(argv[3], value)) {
          throw std::runtime_error("Existing integer key and valid value required");
        }
      }
      writeNew(argv[argc - 1], document->serialize());
      return 0;
    }
    std::cerr << "BiglyBTC compatibility probe\n"
      "  roundtrip INPUT NEW_OUTPUT\n"
      "  set-integer INPUT KEY VALUE NEW_OUTPUT\n"
      "  verify PUBLIC MESSAGE SIGNATURE --legacy\n"
      "  sign PRIVATE PUBLIC MESSAGE NEW_SIGNATURE\n";
    return 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
