// SPDX-License-Identifier: GPL-2.0-or-later
#include "http_media_range.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace biglybtc::media;

namespace {
using Method = MediaHttpMethod;
using Status = MediaHttpRange::Status;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void expect(std::optional<std::string_view> input, std::uint64_t size, Status status,
    std::uint64_t first, std::uint64_t length, std::optional<std::string_view> header,
    bool body = true, Method method = Method::Get) {
  const auto result = planMediaHttpRange(method, size, input);
  require(result && result->fileSize == size && result->status == status && result->offset == first &&
      result->length == length && result->contentRange() == header && result->sendBody == body,
      input.value_or("without a Range header"));
}

void checkedExamples() {
  expect(std::nullopt, 100, Status::Whole, 0, 100, std::nullopt);
  expect("bytes=6-12", 100, Status::Partial, 6, 7, "bytes 6-12/100");
  expect(" \tBYTES=00006-00007\t ", 100, Status::Partial, 6, 2, "bytes 6-7/100");
  expect("bytes=6-", 100, Status::Partial, 6, 94, "bytes 6-99/100");
  expect("bytes=-6", 100, Status::Partial, 94, 6, "bytes 94-99/100");
  expect("bytes=6-999", 100, Status::Partial, 6, 94, "bytes 6-99/100");
  expect("bytes=-999", 100, Status::Partial, 0, 100, "bytes 0-99/100");
  expect("bytes=100-", 100, Status::Unsatisfiable, 0, 0, "bytes */100", false);
  expect("bytes=100-200", 100, Status::Unsatisfiable, 0, 0, "bytes */100", false);
  expect("bytes=-0", 100, Status::Unsatisfiable, 0, 0, "bytes */100", false);
  expect(std::nullopt, 0, Status::Whole, 0, 0, std::nullopt, false);
  expect("bytes=0-", 0, Status::Unsatisfiable, 0, 0, "bytes */0", false);
  expect("bytes=-1", 0, Status::Unsatisfiable, 0, 0, "bytes */0", false);
  expect("bytes=6-12", 100, Status::Whole, 0, 100, std::nullopt, false, Method::Head);
  expect("", 100, Status::Whole, 0, 100, std::nullopt, false, Method::Head);
  expect("seconds=10-20", 100, Status::Whole, 0, 100, std::nullopt);

  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  expect("bytes=18446744073709551614-184467440737095516150", max, Status::Partial, max - 1, 1,
      "bytes 18446744073709551614-18446744073709551614/18446744073709551615");
  expect("bytes=18446744073709551616-", max, Status::Unsatisfiable, 0, 0, "bytes */18446744073709551615", false);
  expect("bytes=-184467440737095516150", max, Status::Partial, 0, max,
      "bytes 0-18446744073709551614/18446744073709551615");

  for (const std::string_view malformed : {"", "bytes", "bytes=", "bytes=-", "bytes=+1-2", "bytes=1--2",
       "bytes=2-1", "bytes=1 -2", "bytes= 1-2", "bytes=1-2\r", "bytes=9000000000000000000000-8000000000000000000000"}) {
    require(planMediaHttpRange(Method::Get, 100, malformed) == std::unexpected(MediaRangeError::Malformed), malformed);
  }
  require(planMediaHttpRange(Method::Get, 100, "bytes=1-2,6-7") == std::unexpected(MediaRangeError::Multiple),
      "A multipart header must leave no range plan");
  require(planMediaHttpRange(Method::Get, 100, std::string(257, '0')) == std::unexpected(MediaRangeError::TooLong),
      "The range input has a fixed bound");
}

void comparisonTable() {
  // Companion oracle reads the exact Original parseRange method. These ranges
  // represent meaningful satisfiable input in both implementations.
  for (const std::uint64_t size : {1, 2, 17, 120, 4096}) {
    for (std::uint64_t first = 0; first < std::min(size, std::uint64_t{12}); ++first) {
      for (std::uint64_t tail = first; tail < std::min(size, first + 9); ++tail) {
        const auto request = "bytes=" + std::to_string(first) + '-' + std::to_string(tail);
        const auto result = planMediaHttpRange(Method::Get, size, request);
        require(result && result->status == Status::Partial, request);
        std::cout << request << '\t' << size << '\t' << result->offset << '\t'
            << result->offset + result->length - 1 << '\t' << result->length << '\n';
      }
      for (const auto& request : {"BYTES=" + std::to_string(first) + '-', "bytes=-" + std::to_string(first + 1)}) {
        const auto result = planMediaHttpRange(Method::Get, size, request);
        require(result && result->status == Status::Partial, request);
        std::cout << request << '\t' << size << '\t' << result->offset << '\t'
            << result->offset + result->length - 1 << '\t' << result->length << '\n';
      }
    }
  }
}
} // namespace

int main(int argc, char**) {
  try {
    checkedExamples();
    if (argc == 2) {
      comparisonTable();
    }
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
