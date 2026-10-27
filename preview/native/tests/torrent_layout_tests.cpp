#include "biglybtc/torrent_layout.hpp"
#include "biglybtc/existing_piece_goal.hpp"

#include <cstdlib>
#include <array>
#include <utility>
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
  if((argc==3||argc==4)&&std::string_view(argv[1])=="--decode-original-filenames"){
    const auto nibble=[](char c)->unsigned{if(c>='0'&&c<='9'){return c-'0';}if(c>='a'&&c<='f'){return c-'a'+10;}if(c>='A'&&c<='F'){return c-'A'+10;}check(false,"Invalid hex fixture");return 0;};
    std::string line;while(std::getline(std::cin,line)){check(line.size()%2==0&&line.size()<=8192,"Bounded hex decoder fixture");std::string raw;for(std::size_t n=0;n<line.size();n+=2){raw+=static_cast<char>((nibble(line[n])<<4)|nibble(line[n+1]));}
      const auto result=compat::originalDecodeFilenameBytes(raw,argv[2],argc==4&&std::string_view(argv[3])=="show-all");if(!result){std::cout<<"ERROR\n";}else{for(const unsigned char c:*result){std::cout<<"0123456789abcdef"[c>>4]<<"0123456789abcdef"[c&15];}std::cout<<'\n';}}
    return 0;
  }
  {
    compat::OriginalLayout layout{"selected",true,16384,65536,{{"left",0,8192,false},{"dnd",8192,49152,false},{"right",57344,8192,false}}};
    const std::array<compat::FilePolicy,3> policy{{{false,0},{true,0},{false,0}}};
    const auto selected=core::existingPieceGoal(layout,policy,core::ExistingVerificationGoal::SelectedFiles,4);
    check(selected&&*selected==std::vector<std::uint8_t>({1,0,0,1}),"Selected boundary pieces need authentic DND edge bytes; interior gaps remain outside goal");
    const auto full=core::existingPieceGoal(layout,policy,core::ExistingVerificationGoal::FullTorrent,4);
    check(full&&*full==std::vector<std::uint8_t>({1,1,1,1}),"Full verification never becomes a selected-files goal");
    const std::array<std::uint8_t,4> actual{1,0,0,1},missingBoundary{1,1,1,0},malformed{1,2,0,1};
    check(core::satisfiesExistingPieceGoal(*selected,actual)&&!core::satisfiesExistingPieceGoal(*full,actual)
        &&!core::satisfiesExistingPieceGoal(*selected,missingBoundary)&&!core::satisfiesExistingPieceGoal(*selected,malformed)
        &&!core::satisfiesExistingPieceGoal(*selected,std::span<const std::uint8_t>(actual).first(3)),"Only one complete binary observed bitfield can satisfy the exact required mask");
    auto none=policy;for(auto& file:none){file.skipped=true;}
    check(!core::existingPieceGoal(layout,none,core::ExistingVerificationGoal::SelectedFiles,4),"No selected bytes is not a fabricated verification success");
    check(!core::existingPieceGoal(layout,policy,core::ExistingVerificationGoal::SelectedFiles,3),"Engine piece count must match exact authenticated layout");
    layout.files[1].offset=8193;check(!core::existingPieceGoal(layout,policy,core::ExistingVerificationGoal::SelectedFiles,4),"Shifted file geometry cannot mint a needed-piece domain");
  }
  if(argc==4&&std::string_view(argv[1])=="--inspect-linux-names"){
    const auto read=[](const char* path,std::size_t limit){std::ifstream stream(path,std::ios::binary);check(static_cast<bool>(stream),"Owned filename fixture unavailable");std::string value(std::istreambuf_iterator<char>(stream),{});check(value.size()<=limit,"Owned filename fixture bound");return value;};
    const auto raw=read(argv[2],1024*1024),conversions=read(argv[3],16384);const auto source=core::Document::parse(raw);check(source.has_value(),"Filename fixture bencode");const auto layout=compat::originalLayout(*source,100000,conversions);check(layout.has_value(),layout?"":layout.error().message.c_str());
    const auto hex=[](std::string_view value){std::string out;for(const unsigned char c:value){out+="0123456789ABCDEF"[c>>4];out+="0123456789ABCDEF"[c&15];}return out;};std::cout<<"NAME\t"<<hex(layout->name)<<'\n';
    for(std::size_t n=0;n<layout->files.size();++n){const auto& file=layout->files[n];std::cout<<"FILE\t"<<n<<'\t'<<file.offset<<'\t'<<file.bytes<<'\t'<<hex(file.path)<<'\n';}check(source->serialize()==raw,"Filename inspection cannot rewrite metadata");return 0;
  }
  {
    const auto replacement=compat::originalDecodeFilenameBytes(std::string("a\x98z",3),"windows-1251");check(replacement&&*replacement=="a\xef\xbf\xbdz","Undefined legacy byte falls back to whole-input Java UTF8 replacement");
    const auto fallback=compat::originalDecodeFilenameBytes(std::string("ABC\xff .bin",9),"Fallback");check(fallback&&*fallback=="ABC_FF_20.bin","Exact source fallback escapes and original case");
    const auto boundary=compat::originalDecodeFilenameBytes(std::string(64,'A'),"Fallback");check(boundary&&*boundary==std::string(64,'A'),"Original unconditional64 lower bound");
    check(!compat::originalDecodeFilenameBytes(std::string(65,'A'),"Fallback")&&!compat::originalDecodeFilenameBytes("ascii","fallback"),"No invented long-name history or case-guessed fallback registration");
    const auto thai=compat::originalDecodeFilenameBytes(std::string("\xa1",1),"TIS-620");check(thai&&*thai=="ก","Source-registered Thai scalar");
    check(!compat::originalDecodeFilenameBytes("ascii","IBM866"),"ShowAll-only decoder stays unregistered without held capability");
    const auto dos=compat::originalDecodeFilenameBytes(std::string("\x80",1),"IBM866",true);check(dos&&*dos=="А","Explicit ShowAll DOS Cyrillic mapping");
    check(!compat::originalDecodeFilenameBytes("ascii","shift jis"),"No punctuation-guessed aliases");
  }
  {
    const std::array<std::array<std::string_view,3>,9> cases{{
      {{"a\"b.bin","\"='","a'b.bin"}},{{"a\"b.bin","","a\"b.bin"}},{{"ab","a=b,b=c","bc"}},{{"a","a=b,a=c","c"}},
      {{"a"," a:b , ignored ,a=x","x"}},{{"a/b\rc\nd","","a b c d"}},{{"quote","\"='","quote"}},{{"a","\xf0\x9f\x98\x80=x","a"}},{{"x","x=\xc3\xa9","\xc3\xa9"}}
    }};
    for(const auto& row:cases){const auto result=compat::originalFilenameComponent(row[0],row[1]);check(result&&*result==row[2],"Exact original UTF16/trim/nonchained Linux conversion");}
    for(const auto& row:std::array<std::array<std::string_view,2>,3>{{{{"a","a=."}},{{"a","a=\\"}},{{"a",std::string_view("a=\xff",3)}}}}){check(!compat::originalFilenameComponent(row[0],row[1]),"Unsafe/invalid configured output remains refused");}
    const auto trimmedNul=compat::originalFilenameComponent("a",std::string_view("a=\0",3));check(trimmedNul&&*trimmedNul=="a","Original Java trim removes trailing NUL so a two-unit mapping row is ignored");
    const auto source=document(list(file("a\"b.bin","1")));const auto raw=source.serialize();const auto legacy=compat::originalLayout(source);const auto converted=compat::originalLayout(source,100000,compat::OriginalDefaultFilenameConversions);
    check(legacy&&legacy->files[0].path=="a\"b.bin"&&converted&&converted->files[0].path=="a'b.bin"&&converted->filenameConverted&&source.serialize()==raw,"Old raw projection and explicit original conversion are distinct; info unchanged");
    check(!compat::originalLayout(document(list(file("a\"b.bin","1")+file("a'b.bin","1"))),100000,compat::OriginalDefaultFilenameConversions),"Conversion collisions refuse before filesystem authority");
    check(!compat::originalLayout(document("d5:filesld6:lengthi1e4:pathl1:aee" "d6:lengthi1e4:pathl1:x1:yeee4:name4:root12:piece lengthi16384ee"),100000,"x=a"),"A converted file cannot become another file's parent");
    check(!compat::originalLayout(document(list(file("a","1")+file("a-foo","1")+"d6:lengthi1e4:pathl1:x1:bee")),100000,"x=a"),"Every converted ancestor is checked even when a-foo sorts between a and a/b");
    for(const auto reserved:{".git",".BiglyBTC.reserved"}){check(!compat::originalFilenameComponent(reserved,""),"Reserved native component is never payload authority");}
    const auto tree=document("d9:file treed1:Ad0:d6:lengthi1eee1:Bd0:d6:lengthi1eeee12:meta versioni2e4:name4:root12:piece lengthi16384ee");
    const auto order=compat::originalLayout(tree,100000,"A=Z,B=Y");check(order&&order->files.front().path=="Z"&&order->files.back().path=="Y","Original raw UTF16/index order precedes path transformation");
  }
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
    auto unknown=*source;check(unknown.replaceValue("encoding","12:windows-1252").has_value(),"Unknown charset fixture");
    check(!compat::originalLayout(unknown),"Unported charset never silently becomes UTF8 or Latin1");
    const auto unsafe="d8:encoding10:ISO-8859-14:infod5:filesl"+file("..","1")+"e4:name1:a12:piece lengthi16384eee";
    const auto bad=core::Document::parse(unsafe);check(bad.has_value()&&!compat::originalLayout(*bad),"Decoded traversal still fails before mapping");
  }
  {
    const std::string root("\xef\xe0\xef\xea\xe0",5),leaf("\xf4\xe0\xe9\xeb.bin",8);
    for(const auto* alias:{"windows-1251","WINDOWS-1251","cp1251","cp5347","ansi-1251"}){
      for(const int overrides:{0,1,2}){
        const auto first="d6:lengthi3e4:pathl"+bytes(leaf)+"e"+(overrides?"10:path.utf-8l9:wrong.bine":"")+'e';
        const auto second=file("plain.bin","5");
        const auto info="d5:filesl"+first+second+"e4:name"+bytes(root)+(overrides==2?"10:name.utf-85:wrong":"")+"12:piece lengthi16384ee";
        const auto raw="d8:encoding"+bytes(alias)+"4:info"+info+'e';auto source=core::Document::parse(raw);check(source.has_value(),"Windows-1251 original format fixture");
        const auto decoded=compat::originalLayout(*source);
        check(decoded&&decoded->name=="папка"&&decoded->files.size()==2&&decoded->files[0].path=="файл.bin"&&decoded->files[1].path=="plain.bin"
            &&decoded->files[1].offset==3&&decoded->bytes==8,"Explicit Windows-1251 ignores UTF8 keys globally and preserves original indices/geometry");
        check(source->serialize()==raw,"Windows-1251 projection never rewrites hashed raw metadata");
      }
    }
    for(const unsigned value:{0U,10U,31U,47U,92U,127U}){
      const auto bad="a"+std::string(1,static_cast<char>(value))+"z";auto source=document(list(file(bad,"1")));
      check(source.replaceValue("encoding",bytes("windows-1251")).has_value(),"Unsafe codec fixture");
      check(!compat::originalLayout(source),"Existing filesystem control/separator refusals remain distinct from raw decoder output");
    }
    for(const auto* alias:{"windows1251","windows_1251","cp-1251","koi8-u"}){
      auto source=document(list(file("safe","1")));check(source.replaceValue("encoding",bytes(alias)).has_value(),"Unqualified alias fixture");
      check(!compat::originalLayout(source),"Only source-proven aliases are admitted");
    }
    auto v2=document("d4:name1:a12:piece lengthi16384e9:file treed1:ad0:d6:lengthi1eeeee");
    check(v2.replaceValue("encoding",bytes("windows-1251")).has_value()&&!compat::originalLayout(v2),"Legacy codec never relaxes v2 UTF8 metadata");
  }
  {
    for(const auto encoding:std::array<std::string_view,4>{"KOI8-R","koi8_r","koi8","cskoi8r"}){
      const auto root=std::string("\xd0\xc1\xd0\xcb\xc1",5);
      const auto leaf=std::string("\xc6\xc1\xca\xcc.bin",8);
      for(const bool overrides:{false,true}){
        const auto first="d6:lengthi3e4:pathl"+bytes(leaf)+"e"+(overrides?"10:path.utf-8l9:wrong.bine":"")+'e';
        auto source=document("d5:filesl"+first+file("ascii.bin","5")+"e4:name"+bytes(root)+(overrides?"10:name.utf-85:wrong":"")+"12:piece lengthi16384ee");
        check(source.replaceValue("encoding",bytes(encoding)).has_value(),"Original declared Cyrillic codec");const auto raw=source.serialize();const auto layout=compat::originalLayout(source);
        check(layout&&layout->name=="папка"&&layout->files.size()==2&&layout->files[0].path=="файл.bin"&&layout->files[1].path=="ascii.bin"&&layout->files[1].offset==3&&layout->bytes==8,"Exact Cyrillic table and global legacy-overrides policy");
        check(source.serialize()==raw,"Cyrillic layout never rewrites source/raw info");
      }
    }
    for(const auto* encoding:{"KOI8-R"}){
      for(const unsigned value:{0U,31U,47U,92U,127U}){auto source=document(list(file("a"+std::string(1,static_cast<char>(value))+"z","1")));check(source.replaceValue("encoding",bytes(encoding)).has_value()&&!compat::originalLayout(source),"Unsafe Cyrillic codec input remains refused");}
      auto v2=document("d4:name1:a12:piece lengthi16384e9:file treed1:ad0:d6:lengthi1eeeee");check(v2.replaceValue("encoding",bytes(encoding)).has_value()&&!compat::originalLayout(v2),"Cyrillic codec cannot relax v2 UTF8");
    }
    // IBM866 is not in the original manual registry; ShowAll defaults false.
    for(const auto* encoding:{"IBM866","cp866","ibm866","ibm-866","866","csIBM866"}){auto source=document(list(file("ascii","1")));const auto before=source.serialize();check(source.replaceValue("encoding",bytes(encoding)).has_value(),"Deferred codec fixture");const auto raw=source.serialize();check(!compat::originalLayout(source)&&source.serialize()==raw&&raw!=before,"IBM866 remains unsupported without original registry context");}
    for(const auto* unqualified:{"koi8r","koi8 r","cp-866","ibm_866","KOI8-U"}){auto source=document(list(file("ascii","1")));check(source.replaceValue("encoding",bytes(unqualified)).has_value()&&!compat::originalLayout(source),"No guessed legacy aliases");}
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
