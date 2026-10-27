#include "biglybtc/torrent_layout.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <optional>
#include <span>

namespace biglybtc::compat {
namespace {
using core::Node;
struct Failure { std::string message; };
void require(bool condition, std::string message) { if (!condition) { throw Failure{std::move(message)}; } }
std::string text(const Node* value) {
  require(value && value->type == Node::Type::Bytes && value->bytes.size() <= 4096,
      "Expected a bounded metadata byte string");
  return std::string(value->bytes);
}
std::int64_t integer(const Node* value) {
  require(value && value->type == Node::Type::Integer && value->integer >= 0, "Expected a nonnegative metadata integer");
  return value->integer;
}
#include "original_charset.inc"
enum class NameEncoding : unsigned { Utf8, Latin1, Windows1251, Koi8R, Utf8Replacement };
struct NamePolicy { NameEncoding encoding; bool utf8Keys; };
NamePolicy names(const core::Document& document,const Node& info,bool showAll=false) {
  const auto* field=document.find("encoding");
  const auto original=field?text(field):std::string{};
  auto lower=original;
  std::ranges::transform(lower,lower.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
  static constexpr std::array<std::string_view,15> latinAliases{"iso-8859-1","iso-ir-100","iso_8859-1","latin1","l1","ibm819","cp819","csisolatin1","819","ibm-819","iso8859_1","iso_8859-1:1987","iso_8859_1","8859_1","iso8859-1"};
  const bool latin1=std::ranges::find(latinAliases,lower)!=latinAliases.end(); // Explicit original canonical charset only.
  // Exact Java17 MS1251 canonical name and its source-declared aliases;
  // no punctuation stripping or guessed aliases for legacy codecs.
  const bool windows1251=lower=="windows-1251"||lower=="cp1251"||lower=="cp5347"||lower=="ansi-1251";
  const bool koi8r=lower=="koi8-r"||lower=="koi8_r"||lower=="koi8"||lower=="cskoi8r";
  const auto manual=charsetIndex(original,showAll);
  require(!field||manual||original=="utf8 keys"||lower=="utf-8"||lower=="utf8"||lower=="unicode-1-1-utf-8"||latin1||windows1251||koi8r,
      "This original filename encoding needs its qualified codec adapter");
  require(!(latin1||windows1251||koi8r||manual)||!info.find("file tree"),"Legacy encoded v2/hybrid names require separate original qualification");
  // Exact cb8 global UTF8-key predicate: name plus every v1 path, and only
  // absent encoding, the exact saved sentinel, or case-insensitive UTF-8.
  bool use=info.find("name.utf-8")&&(!field||original=="utf8 keys"||lower=="utf-8");
  if(const auto* files=info.find("files")){
    require(files->type==Node::Type::List,"Original file list has invalid type");
    if(use){for(const auto& file:files->children){use=use&&file.find("path.utf-8");}}
  }
  return {manual?static_cast<NameEncoding>(16+*manual):koi8r?NameEncoding::Koi8R:windows1251?NameEncoding::Windows1251:latin1?NameEncoding::Latin1:field?NameEncoding::Utf8Replacement:NameEncoding::Utf8,use};
}
std::string decoded(const Node* node,NameEncoding encoding) {
  const auto value=text(node);
  if(static_cast<unsigned>(encoding)>=16){return charsetDecode(value,static_cast<unsigned>(encoding)-16);}
  if(encoding==NameEncoding::Utf8Replacement){return charsetUtf8(value);}
  if(encoding==NameEncoding::Utf8){return value;}
  if(encoding==NameEncoding::Koi8R){
    // Exact OpenJDK17 KOI8-R table, explicitly registered by cb8 LocaleUtil.
    // ASCII is invariant; unsafe filesystem input still refuses.
    static constexpr std::array<std::uint16_t,128> koi8r{
        0x2500,0x2502,0x250c,0x2510,0x2514,0x2518,0x251c,0x2524,
        0x252c,0x2534,0x253c,0x2580,0x2584,0x2588,0x258c,0x2590,
        0x2591,0x2592,0x2593,0x2320,0x25a0,0x2219,0x221a,0x2248,
        0x2264,0x2265,0x00a0,0x2321,0x00b0,0x00b2,0x00b7,0x00f7,
        0x2550,0x2551,0x2552,0x0451,0x2553,0x2554,0x2555,0x2556,
        0x2557,0x2558,0x2559,0x255a,0x255b,0x255c,0x255d,0x255e,
        0x255f,0x2560,0x2561,0x0401,0x2562,0x2563,0x2564,0x2565,
        0x2566,0x2567,0x2568,0x2569,0x256a,0x256b,0x256c,0x00a9,
        0x044e,0x0430,0x0431,0x0446,0x0434,0x0435,0x0444,0x0433,
        0x0445,0x0438,0x0439,0x043a,0x043b,0x043c,0x043d,0x043e,
        0x043f,0x044f,0x0440,0x0441,0x0442,0x0443,0x0436,0x0432,
        0x044c,0x044b,0x0437,0x0448,0x044d,0x0449,0x0447,0x044a,
        0x042e,0x0410,0x0411,0x0426,0x0414,0x0415,0x0424,0x0413,
        0x0425,0x0418,0x0419,0x041a,0x041b,0x041c,0x041d,0x041e,
        0x041f,0x042f,0x0420,0x0421,0x0422,0x0423,0x0416,0x0412,
        0x042c,0x042b,0x0417,0x0428,0x042d,0x0429,0x0427,0x042a
    };
    std::string result;result.reserve(value.size()*3);
    for(const unsigned char c:value){
      const std::uint16_t code=c<0x80?c:koi8r[c-0x80];
      require((code!=0||c==0)&&code!=0xfffd,"Undefined legacy Cyrillic filename byte");
      if(code<0x80){result+=static_cast<char>(code);}
      else if(code<0x800){result+=static_cast<char>(0xc0|(code>>6));result+=static_cast<char>(0x80|(code&0x3f));}
      else{result+=static_cast<char>(0xe0|(code>>12));result+=static_cast<char>(0x80|((code>>6)&0x3f));result+=static_cast<char>(0x80|(code&0x3f));}
    }
    return result;
  }
  if(encoding==NameEncoding::Windows1251){
    // Single-byte Windows-1251 mapping, source-bound to OpenJDK17 MS1251.
    // 0x98 is UNMAPPABLE; never replace it or take Java's UTF8 fallback.
    // 0xc0..0xff are the contiguous Cyrillic U+0410..U+044F block.
    static constexpr std::array<std::uint16_t,64> extended{
        0x0402,0x0403,0x201a,0x0453,0x201e,0x2026,0x2020,0x2021,
        0x20ac,0x2030,0x0409,0x2039,0x040a,0x040c,0x040b,0x040f,
        0x0452,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x0000,0x2122,0x0459,0x203a,0x045a,0x045c,0x045b,0x045f,
        0x00a0,0x040e,0x045e,0x0408,0x00a4,0x0490,0x00a6,0x00a7,
        0x0401,0x00a9,0x0404,0x00ab,0x00ac,0x00ad,0x00ae,0x0407,
        0x00b0,0x00b1,0x0406,0x0456,0x0491,0x00b5,0x00b6,0x00b7,
        0x0451,0x2116,0x0454,0x00bb,0x0458,0x0405,0x0455,0x0457
    };
    std::string result;result.reserve(value.size()*3);
    for(const unsigned char c:value){
      const std::uint16_t code=c<0x80?c:c>=0xc0?static_cast<std::uint16_t>(0x0410+c-0xc0):extended[c-0x80];
      if(code==0&&c!=0){return charsetUtf8(value);}
      if(code<0x80){result+=static_cast<char>(code);}
      else if(code<0x800){result+=static_cast<char>(0xc0|(code>>6));result+=static_cast<char>(0x80|(code&0x3f));}
      else{result+=static_cast<char>(0xe0|(code>>12));result+=static_cast<char>(0x80|((code>>6)&0x3f));result+=static_cast<char>(0x80|(code&0x3f));}
    }
    return result;
  }
  // ISO-8859-1 is a total one-byte Java charset: each byte is its scalar.
  // Filesystem component safety and expanded byte limits are checked later.
  std::string result;result.reserve(value.size()*2);
  for(const unsigned char c:value){
    if(c<0x80){result+=static_cast<char>(c);}
    else{result+=static_cast<char>(0xc0|(c>>6));result+=static_cast<char>(0x80|(c&0x3f));}
  }
  return result;
}
std::u16string utf16(std::string_view value) {
  std::u16string result;
  for (std::size_t i = 0; i < value.size();) {
    const auto first = static_cast<unsigned char>(value[i++]);
    std::uint32_t code = first;
    int count = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { code = first & 0x1f; count = 1; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { code = first & 0x0f; count = 2; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { code = first & 7; count = 3; minimum = 0x10000; }
    else { require(first < 0x80, "Legacy or invalid UTF8 metadata names need their original codec adapter"); }
    for (int j = 0; j < count; ++j) {
      require(i < value.size(), "Truncated UTF8 metadata name");
      const auto next = static_cast<unsigned char>(value[i++]);
      require((next & 0xc0) == 0x80, "Invalid UTF8 metadata name");
      code = (code << 6) | (next & 0x3f);
    }
    require(code >= minimum && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff), "Invalid UTF8 metadata scalar");
    if (code < 0x10000) { result.push_back(static_cast<char16_t>(code)); }
    else {
      code -= 0x10000;
      result.push_back(static_cast<char16_t>(0xd800 + (code >> 10)));
      result.push_back(static_cast<char16_t>(0xdc00 + (code & 1023)));
    }
  }
  return result;
}
struct FilenameConversion { std::vector<std::pair<char16_t,char16_t>> rows; };
FilenameConversion filenameConversion(std::string_view encoded) {
  require(encoded.size()<=16384,"Original filename conversion policy exceeds its bound");
  const auto input=utf16(encoded);FilenameConversion result;
  std::size_t start=0;
  while(start<=input.size()){
    auto end=input.find(u',',start);if(end==std::u16string::npos){end=input.size();}
    auto first=start,last=end;while(first<last&&input[first]<=0x20){++first;}while(last>first&&input[last-1]<=0x20){--last;}
    // Java String.length/trim, not Unicode scalar count or whitespace classes.
    // The middle character is intentionally not validated by the original.
    if(last-first==3){require(result.rows.size()<4096,"Original filename conversion row bound");result.rows.emplace_back(input[first],input[first+2]);}
    if(end==input.size()){break;}start=end+1;
  }
  return result;
}
std::string convertedFilename(std::string_view value,const FilenameConversion& policy){
  auto units=utf16(value);
  for(auto& unit:units){const auto original=unit;
    for(const auto& [from,to]:policy.rows){if(original==from){unit=to;}} // Last matching row; never chained.
    if(unit==u'/'||unit==u'\r'||unit==u'\n'){unit=u' ';} // Exact Linux branch.
  }
  std::string result;result.reserve(value.size());
  for(std::size_t n=0;n<units.size();++n){std::uint32_t code=units[n];
    if(code>=0xd800&&code<=0xdbff){require(n+1<units.size()&&units[n+1]>=0xdc00&&units[n+1]<=0xdfff,"Filename conversion produced an unpaired surrogate");code=0x10000+((code-0xd800)<<10)+(units[++n]-0xdc00);}
    else{require(code<0xdc00||code>0xdfff,"Filename conversion produced an unpaired surrogate");}
    if(code<0x80){result+=static_cast<char>(code);}else if(code<0x800){result+=static_cast<char>(0xc0|(code>>6));result+=static_cast<char>(0x80|(code&63));}
    else if(code<0x10000){result+=static_cast<char>(0xe0|(code>>12));result+=static_cast<char>(0x80|((code>>6)&63));result+=static_cast<char>(0x80|(code&63));}
    else{result+=static_cast<char>(0xf0|(code>>18));result+=static_cast<char>(0x80|((code>>12)&63));result+=static_cast<char>(0x80|((code>>6)&63));result+=static_cast<char>(0x80|(code&63));}
  }
  return result;
}
std::string component(std::string value,const FilenameConversion* policy=nullptr,bool* changed=nullptr) {
  if(policy){auto converted=convertedFilename(value,*policy);if(changed&&converted!=value){*changed=true;}value=std::move(converted);}
  require(!value.empty() && value.size() <= 255 && value != "." && value != ".."
      && value.find_first_of(std::string("/\\\n\0", 4)) == std::string::npos,
      "Unsafe or unqualified original path component");
  utf16(value);
  if(policy){require(value!=".git"&&!value.starts_with(".BiglyBTC"),"Converted path overlaps reserved native storage");}
  return value;
}
void append(OriginalLayout& result, std::string path, std::int64_t size, bool padding, std::size_t limit) {
  require(result.files.size() < limit && size <= std::numeric_limits<std::int64_t>::max() - result.bytes,
      "Original layout exceeds file/offset limits");
  require(path.size() <= 4096, "Original relative path exceeds the bounded path limit");
  result.files.push_back({std::move(path), result.bytes, size, padding});
  result.bytes += size;
}
void tree(OriginalLayout& result, const Node& node, const std::string& path,
    std::size_t limit, std::size_t depth, std::size_t& pads,const FilenameConversion* policy) {
  require(node.type == Node::Type::Dictionary && depth <= 128, "Invalid or overdeep v2 file tree");
  std::vector<std::pair<std::u16string, std::size_t>> order;
  for (std::size_t i = 0; i < node.children.size(); i += 2) {
    require(node.children[i].bytes.size() <= 255, "V2 path component exceeds its limit");
    order.emplace_back(utf16(node.children[i].bytes), i);
  }
  std::ranges::sort(order, {}, &std::pair<std::u16string, std::size_t>::first);
  for (const auto& [key, index] : order) {
    static_cast<void>(key);
    const auto& name = node.children[index].bytes;
    const auto& child = node.children[index + 1];
    if (name.empty()) {
      require(node.children.size() == 2 && !path.empty() && child.type == Node::Type::Dictionary,
          "A v2 file leaf cannot also contain child files");
      require(!child.find("symlink path"), "Symbolic metadata files require their qualified storage policy");
      if (const auto* flags = child.find("attr")) {
        const auto value = text(flags);
        require(value.find_first_of("lp") == std::string::npos, "Explicit v2 link/padding attributes need their original policy");
      }
      const auto excess = result.bytes % result.pieceLength;
      if (!result.files.empty() && excess != 0) {
        const auto size = result.pieceLength - excess;
        append(result, ".pad/" + std::to_string(++pads) + '_' + std::to_string(size), size, true, limit);
      }
      append(result, path, integer(child.find("length")), false, limit);
    } else {
      const auto part = component(std::string(name),policy,&result.filenameConverted);
      tree(result, child, path.empty() ? part : path + '/' + part, limit, depth + 1, pads,policy);
    }
  }
}
}

static std::expected<OriginalLayout, LayoutError> originalLayoutImpl(const core::Document& document, std::size_t maximumFiles,std::optional<std::string_view> conversions,bool showAll=false) {
  std::optional<FilenameConversion> conversion;
  try {
    if(conversions){conversion=filenameConversion(*conversions);}
    const auto* conversionPolicy=conversion?&*conversion:nullptr;bool nameChanged=false;
    const auto* info = document.find("info");
    require(info && info->type == Node::Type::Dictionary, "Original metadata has no info dictionary");
    const auto policy=names(document,*info,showAll);
    const auto* name = policy.utf8Keys?info->find("name.utf-8"):info->find("name");
    OriginalLayout result{component(decoded(name,policy.utf8Keys?NameEncoding::Utf8:policy.encoding),conversionPolicy,&nameChanged), false,
        integer(info->find("piece length")), 0, {},nameChanged};
    require(result.pieceLength > 0 && result.pieceLength <= OriginalMaximumPieceLength, "Unqualified original piece length");
    if (const auto* files = info->find("files")) {
      require(files->type == Node::Type::List && !files->children.empty(), "Invalid original v1 file list");
      result.multifile = true;
      for (const auto& file : files->children) {
        require(file.type == Node::Type::Dictionary && !file.find("symlink path"), "Invalid or symbolic original file entry");
        const auto* components = file.find(policy.utf8Keys?"path.utf-8":"path");
        require(components && components->type == Node::Type::List && !components->children.empty(), "Original file has no component list");
        std::string path;
        for (const auto& part : components->children) {
          require(part.type == Node::Type::Bytes && path.size() + part.bytes.size() + 1 <= 4096,
              "Original relative path exceeds its limit");
          if (!path.empty()) { path += '/'; }
          path += component(decoded(&part,policy.utf8Keys?NameEncoding::Utf8:policy.encoding),conversionPolicy,&result.filenameConverted);
        }
        bool padding = false;
        if (const auto* flags = file.find("attr")) {
          const auto value = text(flags);
          require(value.find('l') == std::string::npos, "Symbolic original files require their qualified storage policy");
          padding = value.find('p') != std::string::npos;
        }
        append(result, std::move(path), integer(file.find("length")), padding, maximumFiles);
      }
    } else if (const auto* size = info->find("length")) {
      append(result, result.name, integer(size), false, maximumFiles);
    } else if (const auto* files = info->find("file tree")) {
      std::size_t pads = 0;
      tree(result, *files, {}, maximumFiles, 0, pads,conversionPolicy);
      require(!result.files.empty(), "Original v2 file tree is empty");
      result.multifile = result.files.size() != 1 || result.files[0].path != result.name;
    } else { throw Failure{"Original metadata has no file layout"}; }
    require(result.files.size() <= maximumFiles, "Original layout file count exceeds its limit");
    std::set<std::string> physical;
    for (const auto& file : result.files) {
      require(file.padding || physical.insert(file.path).second, "Two original data files alias the same relative path");
    }
    if(conversionPolicy){for(const auto& path:physical){for(auto slash=path.find('/');slash!=std::string::npos;slash=path.find('/',slash+1)){require(!physical.contains(path.substr(0,slash)),"Converted file is also another file's parent");}}}
    return result;
  } catch (const Failure& failure) { return std::unexpected(LayoutError{failure.message}); }
}
std::expected<OriginalLayout,LayoutError> originalLayout(const core::Document& document,std::size_t maximumFiles){return originalLayoutImpl(document,maximumFiles,{});}
std::expected<OriginalLayout,LayoutError> originalLayout(const core::Document& document,std::size_t maximumFiles,std::string_view conversions){return originalLayoutImpl(document,maximumFiles,conversions);}
std::expected<OriginalLayout,LayoutError> originalLayout(const core::Document& document,std::size_t maximumFiles,std::string_view conversions,bool showAll){return originalLayoutImpl(document,maximumFiles,conversions,showAll);}
std::expected<std::string,LayoutError> originalDecodeFilenameBytes(std::string_view value,std::string_view encoding,bool showAll){
  try{
    require(value.size()<=4096&&encoding.size()<=128,"Original decoder input exceeds bound");
    const auto doc=core::Document::parse("d8:encoding"+std::to_string(encoding.size())+":"+std::string(encoding)+"4:infodee");
    require(doc.has_value(),"Invalid original decoder name");const auto policy=names(*doc,*doc->find("info"),showAll);
    Node node;node.type=Node::Type::Bytes;node.bytes=value;return decoded(&node,policy.encoding);
  }catch(const Failure& error){return std::unexpected(LayoutError{error.message});}
}
std::expected<std::string,LayoutError> originalFilenameComponent(std::string_view decoded,std::string_view conversions){
  try{const auto policy=filenameConversion(conversions);return component(std::string(decoded),&policy);}catch(const Failure& failure){return std::unexpected(LayoutError{failure.message});}
}
} // namespace biglybtc::compat
