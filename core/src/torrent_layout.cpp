#include "biglybtc/torrent_layout.hpp"

#include <algorithm>
#include <limits>
#include <set>

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
enum class NameEncoding { Utf8, Latin1 };
struct NamePolicy { NameEncoding encoding; bool utf8Keys; };
NamePolicy names(const core::Document& document,const Node& info) {
  const auto* field=document.find("encoding");
  const auto original=field?text(field):std::string{};
  auto normalized=original;
  std::erase_if(normalized,[](unsigned char c){return c=='-'||c=='_'||c==' ';});
  std::ranges::transform(normalized,normalized.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
  auto lower=original;
  std::ranges::transform(lower,lower.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
  const bool latin1=lower=="iso-8859-1"; // Explicit original canonical charset only.
  require(!field||normalized=="utf8"||normalized=="utf8keys"||latin1,
      "This original filename encoding needs its qualified codec adapter");
  require(!latin1||!info.find("file tree"),"Legacy encoded v2/hybrid names require separate original qualification");
  // Exact cb8 global UTF8-key predicate: name plus every v1 path, and only
  // absent encoding, the exact saved sentinel, or case-insensitive UTF-8.
  bool use=info.find("name.utf-8")&&(!field||original=="utf8 keys"||lower=="utf-8");
  if(const auto* files=info.find("files")){
    require(files->type==Node::Type::List,"Original file list has invalid type");
    if(use){for(const auto& file:files->children){use=use&&file.find("path.utf-8");}}
  }
  return {latin1?NameEncoding::Latin1:NameEncoding::Utf8,use};
}
std::string decoded(const Node* node,NameEncoding encoding) {
  const auto value=text(node);
  if(encoding==NameEncoding::Utf8){return value;}
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
std::string component(std::string value) {
  require(!value.empty() && value.size() <= 255 && value != "." && value != ".."
      && value.find_first_of(std::string("/\\\n\0", 4)) == std::string::npos,
      "Unsafe or unqualified original path component");
  utf16(value);
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
    std::size_t limit, std::size_t depth, std::size_t& pads) {
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
      const auto part = component(std::string(name));
      tree(result, child, path.empty() ? part : path + '/' + part, limit, depth + 1, pads);
    }
  }
}
}

std::expected<OriginalLayout, LayoutError> originalLayout(const core::Document& document, std::size_t maximumFiles) {
  try {
    const auto* info = document.find("info");
    require(info && info->type == Node::Type::Dictionary, "Original metadata has no info dictionary");
    const auto policy=names(document,*info);
    const auto* name = policy.utf8Keys?info->find("name.utf-8"):info->find("name");
    OriginalLayout result{component(decoded(name,policy.utf8Keys?NameEncoding::Utf8:policy.encoding)), false,
        integer(info->find("piece length")), 0, {}};
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
          path += component(decoded(&part,policy.utf8Keys?NameEncoding::Utf8:policy.encoding));
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
      tree(result, *files, {}, maximumFiles, 0, pads);
      require(!result.files.empty(), "Original v2 file tree is empty");
      result.multifile = result.files.size() != 1 || result.files[0].path != result.name;
    } else { throw Failure{"Original metadata has no file layout"}; }
    require(result.files.size() <= maximumFiles, "Original layout file count exceeds its limit");
    std::set<std::string> physical;
    for (const auto& file : result.files) {
      require(file.padding || physical.insert(file.path).second, "Two original data files alias the same relative path");
    }
    return result;
  } catch (const Failure& failure) { return std::unexpected(LayoutError{failure.message}); }
}
} // namespace biglybtc::compat
