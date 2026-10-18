#include "biglybtc/torrent_layout.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
using namespace biglybtc;
void check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
core::Document document(const std::string& info) {
  auto result = core::Document::parse("d4:info" + info + 'e');
  check(result.has_value(), "Geometry test fixture must parse");
  return std::move(*result);
}
std::string file(std::string_view path, std::string_view size) { return "d6:lengthi" + std::string(size) + "e4:pathl" + bytes(path) + "ee"; }
std::string list(const std::string& entries) { return "d4:name6:bundle12:piece lengthi16384e5:filesl" + entries + "ee"; }
}

int main(int argc, char** argv) {
  auto simple = document("d4:name1:a6:lengthi3e12:piece lengthi16384ee");
  auto result = compat::originalLayout(simple);
  check(result && !result->multifile && result->bytes == 3 && result->files[0].path == "a", "Original simple geometry");
  auto multiple = document(list(file("a", "20000") + file("b", "111072") + file("empty", "0")));
  result = compat::originalLayout(multiple);
  check(result && result->multifile && result->files.size() == 3 && result->bytes == 131072
      && result->files[1].offset == 20000 && result->files[2].bytes == 0, "V1 ordered files preserve cross-file offsets and empty files");
  check(!compat::originalLayout(multiple, 2), "Original file count is bounded");
  const std::string bmp("\xee\x80\x80", 3);
  const std::string astral("\xf0\x90\x80\x80", 4);
  const auto leaf = std::string("d0:d6:lengthi1eee");
  auto tree = document("d4:name6:bundle12:piece lengthi16384e9:file treed" + bytes(bmp) + leaf + bytes(astral) + leaf + "ee");
  result = compat::originalLayout(tree);
  check(result && result->files.size() == 3 && result->files[0].path == astral && result->files[2].path == bmp,
      "V2 original Java UTF16 order differs from UTF8 byte order for supplementary names");
  check(result->files[1].padding && result->files[1].path == ".pad/1_16383" && result->files[2].offset == 16384,
      "Original synthetic padding index/name/alignment");
  check(tree.serialize().find(astral) != std::string::npos, "Layout queries preserve original raw metadata");
  check(!compat::originalLayout(document(list(file("..", "1")))), "Traversal component rejected");
  check(!compat::originalLayout(document(list(file("a/b", "1")))), "Embedded component separator rejected");
  check(!compat::originalLayout(document(list(file(std::string("a\0b", 3), "1")))), "NUL component rejected");
  check(!compat::originalLayout(document(list(file(std::string("\xc0\xae", 2), "1")))), "Overlong UTF8 cannot disguise traversal");
  check(!compat::originalLayout(document(list(file("a", "1") + file("a", "2")))), "Duplicate data paths rejected");
  check(!compat::originalLayout(document(list(file("a", "9223372036854775807") + file("b", "1")))), "Offset overflow rejected");
  check(!compat::originalLayout(document("d4:name1:a6:lengthi1e12:piece lengthi0ee")), "Zero piece geometry rejected");
  check(!compat::originalLayout(document("d4:name1:a6:lengthi-1e12:piece lengthi16384ee")), "Negative file size rejected");
  if (argc > 1) {
    check(argc == 4, "Pass the three generated original metadata variants");
    for (int i = 1; i < argc; ++i) {
      std::ifstream stream(argv[i], std::ios::binary);
      check(static_cast<bool>(stream), "Original Java metadata fixture unavailable");
      const std::string encoded(std::istreambuf_iterator<char>(stream), {});
      auto source = core::Document::parse(encoded);
      check(source.has_value(), "Original Java metadata must parse");
      const auto layout = compat::originalLayout(*source);
      check(layout.has_value() && layout->multifile, "Original Java multifile geometry decoded");
      check(layout->bytes == (i == 1 ? 131072 : 147456) && layout->files.size() == (i == 1 ? 3U : 5U), "Original Java counts/padded sizes agree");
      check(layout->files[i == 1 ? 1 : 2].path == std::string("nested/02-\xd7\xa9\xd8\xa8.bin"), "Original Unicode path bytes agree");
      if (i != 1) { check(layout->files[1].path == ".pad/1_12768" && layout->files[3].path == ".pad/2_3616", "Original Java pad names retained"); }
      check(source->serialize() == encoded, "Geometry read leaves original metadata bytes exact");
    }
  }
  std::cout << "PASS original file geometry, UTF16 ordering, padding and malformed/overflow boundaries\n";
}
