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
  if(argc>=3&&std::string_view(argv[1])=="--inspect-names"){
    check(argc==3,"Exactly one generated metadata input required");
    std::ifstream stream(argv[2],std::ios::binary);check(static_cast<bool>(stream),"Generated name fixture unavailable");
    const std::string bytes(std::istreambuf_iterator<char>(stream),{});check(bytes.size()<=1024*1024,"Name fixture bound");
    auto source=core::Document::parse(bytes);check(source.has_value(),"Name fixture bencode");
    const auto layout=compat::originalLayout(*source);check(layout.has_value(),"Name fixture original layout");
    const auto hex=[](std::string_view value){std::string out;for(const unsigned char c:value){out+="0123456789ABCDEF"[c>>4];out+="0123456789ABCDEF"[c&15];}return out;};
    std::cout<<"NAME\t"<<hex(layout->name)<<'\n';
    for(std::size_t i=0;i<layout->files.size();++i){const auto& file=layout->files[i];
      std::cout<<"FILE\t"<<i<<'\t'<<file.offset<<'\t'<<file.bytes<<'\t'<<hex(file.path)<<'\n';}
    check(source->serialize()==bytes,"Name inspection preserves exact raw metadata");return 0;
  }
  {
    const std::string latinName("caf\xe9",4),latinFile("na\xefve.bin",9);
    const auto entry="d6:lengthi3e4:pathl"+bytes(latinFile)+"ee";
    const auto info="d5:filesl"+entry+"e4:name"+bytes(latinName)+"12:piece lengthi16384ee";
    const auto raw="d8:encoding10:ISO-8859-14:info"+info+'e';
    auto source=core::Document::parse(raw);check(source.has_value(),"Latin1 literal fixture parses");
    const auto layout=compat::originalLayout(*source);
    check(layout&&layout->name=="caf\xc3\xa9"&&layout->files[0].path=="na\xc3\xafve.bin","Original Latin1 byte values decode to actual Unicode filesystem names");
    check(source->serialize()==raw,"Legacy decoding does not rewrite hashed info bytes");
    const auto legacyOverride="d8:encoding10:ISO-8859-14:infod5:filesld6:lengthi3e4:pathl"+bytes(latinFile)
        +"e10:path.utf-8l7:ignoredeee4:name"+bytes(latinName)+"10:name.utf-87:ignored12:piece lengthi16384eee";
    auto ignored=core::Document::parse(legacyOverride);check(ignored.has_value(),"Ignored overrides fixture parses");
    const auto fallback=compat::originalLayout(*ignored);
    check(fallback&&fallback->name==layout->name&&fallback->files[0].path==layout->files[0].path,"Explicit legacy encoding disables all UTF8 overrides as original does");
    const auto mixed="d4:infod5:filesld6:lengthi1e4:pathl1:ae10:path.utf-8l1:xee"+file("b","1")
        +"e4:name4:base10:name.utf-85:other12:piece lengthi16384eee";
    auto partial=core::Document::parse(mixed);check(partial.has_value(),"Partial override literal parses");
    const auto allOrNone=compat::originalLayout(*partial);
    check(allOrNone&&allOrNone->name=="base"&&allOrNone->files[0].path=="a"&&allOrNone->files[1].path=="b","Missing one path override disables name and all other overrides");
    const auto complete="d4:infod5:filesld6:lengthi1e4:pathl1:ae10:path.utf-8l1:xee"
        "d6:lengthi1e4:pathl1:be10:path.utf-8l1:yee""e4:name4:base10:name.utf-85:other12:piece lengthi16384eee";
    auto preferred=core::Document::parse(complete);check(preferred.has_value(),"Complete override literal parses");
    const auto utf8=compat::originalLayout(*preferred);
    check(utf8&&utf8->name=="other"&&utf8->files[0].path=="x"&&utf8->files[1].path=="y","Complete original UTF8 override set is preferred");
    auto unknown=*source;check(unknown.replaceValue("encoding","12:windows-1251").has_value(),"Unknown charset fixture");
    check(!compat::originalLayout(unknown),"Unported charset never silently becomes UTF8 or Latin1");
    const auto unsafe="d8:encoding10:ISO-8859-14:infod5:filesl"+file("..","1")+"e4:name1:a12:piece lengthi16384eee";
    const auto bad=core::Document::parse(unsafe);check(bad.has_value()&&!compat::originalLayout(*bad),"Decoded traversal still fails before mapping");
  }
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
  for (const std::int64_t piece : {64LL*1024*1024,128LL*1024*1024,256LL*1024*1024}) {
    auto large=document("d4:name1:a6:lengthi"+std::to_string(piece+7)+"e12:piece lengthi"+std::to_string(piece)+"ee");
    const auto original=large.serialize();const auto layout=compat::originalLayout(large);
    check(layout&&layout->pieceLength==piece&&layout->bytes==piece+7,"Original 64/128/256 MiB geometry is supported without payload allocation");
    check(large.serialize()==original,"Large geometry preserves raw original metadata");
    auto padded=document("d4:name6:bundle12:piece lengthi"+std::to_string(piece)+"e9:file treed1:ad0:d6:lengthi1eee1:bd0:d6:lengthi1eeeee");
    const auto geometry=compat::originalLayout(padded);
    check(geometry&&geometry->files.size()==3&&geometry->files[1].padding&&geometry->files[1].bytes==piece-1&&geometry->files[2].offset==piece,"Large v2 synthetic padding uses checked original offsets");
  }
  check(!compat::originalLayout(document("d4:name1:a6:lengthi1e12:piece lengthi268435457ee")),"Above original 256 MiB supported boundary still rejected");
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
