#include "biglybtc/original_file_links.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace biglybtc;
using Code = compat::OriginalFileLinksErrorCode;
using Diagnostic = compat::OriginalFileLinkDiagnosticCode;
void check(bool ok, const char* message) {
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
core::Document rows(std::initializer_list<std::string_view> values) {
  std::string encoded = "l";
  for (const auto value : values) { encoded += bytes(value); }
  auto document = core::Document::parse(encoded + 'e');
  check(document.has_value(), "Invalid original-link test fixture");
  return std::move(*document);
}
auto resolve(const core::Document& document) {
  const std::vector<std::string> bases{"/tmp/biglybtc-links/a", "/tmp/biglybtc-links/b", "/tmp/biglybtc-links/π"};
  return compat::resolveOriginalFileLinks(&document.root(), bases);
}
bool diagnostic(const compat::OriginalFileLinks& result, Diagnostic code, std::size_t row) {
  for (const auto& value : result.diagnostics) { if (value.code == code && value.row == row) { return true; } }
  return false;
}
} // namespace

int main() {
  using namespace biglybtc;
  const std::vector<std::string> bases{"/tmp/biglybtc-links/a", "/tmp/biglybtc-links/b", "/tmp/biglybtc-links/π"};
  auto absent = compat::resolveOriginalFileLinks(nullptr, bases);
  check(absent && absent->byIndex.size() == 3 && !absent->byIndex[0] && absent->diagnostics.empty(), "Absent filelinks2 must use original bases");
  {
    auto document = rows({"-1\n/tmp/biglybtc-links/b\n/external/b", " -2147483648\t\n/tmp/biglybtc-links/π\n/external/π"});
    const auto raw = document.serialize(); auto result = resolve(document);
    check(result && !result->byIndex[0] && result->byIndex[1] && result->byIndex[2], "Negative indices must resolve by exact original file paths");
    check(result->byIndex[1]->migrated && result->byIndex[1]->target == "/external/b" && result->byIndex[2]->sourceRow == 1, "Migrated links lost actual source/index/provenance");
    check(document.serialize() == raw, "Read-only migration rewrote original state");
  }
  {
    auto document = rows({"0\n/stale/from\n/first", "-9\n/tmp/biglybtc-links/a\n/second", "+0000\n/other/from\n/last"});
    auto result = resolve(document);
    check(result && result->byIndex[0] && result->byIndex[0]->target == "/last" && result->byIndex[0]->from == "/other/from" && !result->byIndex[0]->migrated, "Indexed stale from must not override original index authority; ordered last row wins");
  }
  {
    auto document = rows({"0\n/from\n/kept", "-1\n/tmp/biglybtc-links/missing\n/rejected", "2147483648\n/from\n/rejected", "99\n/from\n/rejected", "0", "--1\n/from\n/rejected"});
    auto result = resolve(document);
    check(result && result->byIndex[0] && result->byIndex[0]->target == "/kept", "Ignored original rows erased or changed a prior valid mapping");
    check(diagnostic(*result, Diagnostic::UnresolvedNegative, 1) && diagnostic(*result, Diagnostic::InvalidIndex, 2)
        && diagnostic(*result, Diagnostic::IndexedOutsideMetadata, 3) && diagnostic(*result, Diagnostic::MalformedRow, 4)
        && diagnostic(*result, Diagnostic::InvalidIndex, 5), "Original ignored rows must remain diagnosable");
  }
  {
    auto document = rows({"0\n/from\n/kept", "0\n", "0\n\n", "-1\n/tmp/biglybtc-links/a/\n/rejected", "-1\n/tmp/biglybtc-links//a\n/rejected"});
    auto result = resolve(document);
    check(result && result->byIndex[0] && result->byIndex[0]->target == "/kept", "Java trailing empty split fields or unnormalized negative from were guessed");
  }
  for (const auto reset : {"0\n/from", "0\n/from\n", "0\n/from\n\n", "0\n/same\n/same", "-1\n/tmp/biglybtc-links/a\n/tmp/biglybtc-links/a"}) {
    auto document = rows({"0\n/from\n/kept", reset}); auto result = resolve(document);
    check(result && !result->byIndex[0], "Original ordered remove/reset did not erase prior link");
  }
  {
    auto document = rows({"0\n\n/target", "1\n/from\n\nignored", "2\n/a//b\n/a/b\nignored"});
    auto result = resolve(document);
    check(result && result->byIndex[0] && result->byIndex[0]->from.empty() && result->byIndex[0]->target == "/target", "Null original from must preserve indexed link");
    check(result->byIndex[1] && result->byIndex[1]->target.empty(), "Interior empty target is an unsafe LINK, not a reset; caller must reject it");
    check(result->byIndex[2] && result->byIndex[2]->target == "/a/b" && diagnostic(*result, Diagnostic::ExtraFieldsIgnored, 2), "FileKey raw equality or ignored extra fields changed");
  }
  {
    const std::string controlIndex = std::string(1, '\0') + "\t-0\r \n\n/target";
    auto document = rows({controlIndex}); auto result = resolve(document);
    check(result && result->byIndex[0] && result->byIndex[0]->target == "/target", "Java signed zero/trim code units differ");
  }
  {
    auto parsed = core::Document::parse("li1e" + bytes("0\n/from\n/actual") + 'e');
    check(parsed.has_value(), "Mixed row fixture did not parse"); auto result = resolve(*parsed);
    check(result && result->byIndex[0] && diagnostic(*result, Diagnostic::NonStringRow, 0), "Non-string original list values should be skipped without suppressing actual links");
  }
  {
    const std::vector<std::string> duplicate{"/tmp/biglybtc-links/a", "/tmp/biglybtc-links/a"};
    auto document = rows({"-1\n/tmp/biglybtc-links/a\n/external"});
    auto result = compat::resolveOriginalFileLinks(&document.root(), duplicate);
    check(!result && result.error().code == Code::AmbiguousBase && result.error().row == 0, "Ambiguous migrated path must fail closed before selecting a file");
    document = rows({"1\n/obsolete\n/external"});
    result = compat::resolveOriginalFileLinks(&document.root(), duplicate);
    check(result && result->byIndex[1], "Indexed authority must not be replaced by from-path resolution");
  }
  {
    core::Node wrong(core::Node::Type::Dictionary);
    auto result = compat::resolveOriginalFileLinks(&wrong, bases);
    check(!result && result.error().code == Code::Type, "Wrong filelinks2 root was accepted");
    const std::vector<std::string> relative{"not/absolute"};
    result = compat::resolveOriginalFileLinks(nullptr, relative);
    check(!result && result.error().code == Code::InvalidBase, "A relative original base acquired path authority");
    const std::vector<std::string> many(compat::MaximumOriginalLinkFiles + 1);
    result = compat::resolveOriginalFileLinks(nullptr, many);
    check(!result && result.error().code == Code::Limit, "File-count bound was checked after allocation/use");
    auto document = rows({std::string(compat::MaximumOriginalLinkRowBytes + 1, 'x')}); result = resolve(document);
    check(!result && result.error().code == Code::Limit, "Unbounded original link row was accepted");
    document = rows({std::string("0\n/from\n/") + static_cast<char>(0xff)}); result = resolve(document);
    check(!result && result.error().code == Code::Encoding, "Malformed UTF-8 must not be normalized into filename authority");
  }
  auto owned = [] {
    auto document = rows({"0\n/from\n/external"}); return resolve(document);
  }();
  check(owned && owned->byIndex[0] && owned->byIndex[0]->from == "/from" && owned->byIndex[0]->target == "/external", "Result borrowed a dead Document or base-path buffer");
  std::cout << "PASS original signed file-link migration, exact bases, ordered put/reset, diagnostics and authority bounds\n";
}
