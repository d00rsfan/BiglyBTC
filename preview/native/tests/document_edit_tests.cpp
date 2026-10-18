#include "biglybtc/bencode.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

using namespace biglybtc::core;

namespace {
void require(bool value) {
  if (!value) { throw std::runtime_error("Structured document edit check failed"); }
}
}

int main() {
  try {
    const std::string binary("\0\xff\x80\x7f", 4);
    const auto original = std::string("d4:infod4:name4:teste7:unknown4:") + binary + "e";
    auto document = Document::parse(original);
    require(document.has_value());
    auto untouched = *document;
    require(document->replaceValue("resume", "d4:datad5:validi1eee").has_value());
    require(document->find("resume")->find("data")->find("valid")->integer == 1);
    require(document->serialize() == original.substr(0, original.size() - 1) + "6:resumed4:datad5:validi1eeee");
    require(untouched.serialize() == original);
    require(encodeValue(*document->find("info")) == "d4:name4:teste");
    require(document->find("unknown")->bytes == binary);

    // Patch a nested resume value in bounded independent documents, then splice
    // it back without re-encoding the original torrent info or unknown fields.
    auto encodedResume = encodeValue(*document->find("resume"));
    auto resume = Document::parse(*encodedResume);
    auto data = Document::parse(*encodeValue(*resume->find("data")));
    require(data && data->setInteger("valid", 0));
    require(resume->replaceValue("data", data->serialize()).has_value());
    require(document->replaceValue("resume", resume->serialize()).has_value());
    auto copy = *document;
    document = std::unexpected(ParseError{"released_for_test", 0});
    require(copy.find("resume")->find("data")->find("valid")->integer == 0);
    auto moved = std::move(copy);
    require(moved.find("unknown")->bytes == binary);
    require(encodeValue(moved.root()) == moved.serialize());
    require(encodeValue(*moved.find("info")) == "d4:name4:teste");

    const auto before = moved.serialize();
    auto rejected = moved.replaceValue("resume", "di1ei2ee");
    require(!rejected && rejected.error() == EditError::InvalidValue && moved.serialize() == before);
    auto list = Document::parse("le");
    rejected = list->replaceValue("a", "i1e");
    require(!rejected && rejected.error() == EditError::NotDictionary && list->serialize() == "le");

    auto shallow = Document::parse("de", Limits{.depth = 1});
    rejected = shallow->replaceValue("a", "li1ee");
    require(!rejected && rejected.error() == EditError::Limit && shallow->serialize() == "de");
    require(shallow->replaceValue("a", "i1e").has_value());
    auto nodes = Document::parse("de", Limits{.nodes = 3});
    require(nodes->replaceValue("a", "i1e").has_value());
    rejected = nodes->replaceValue("b", "i2e");
    require(!rejected && rejected.error() == EditError::Limit && nodes->serialize() == "d1:ai1ee");
    require(nodes->replaceValue("a", "i999e").has_value());
    auto bytes = Document::parse("de", Limits{.inputBytes = 8});
    require(bytes->replaceValue("a", "i0e").has_value());
    require(!bytes->setInteger("a", 10) && bytes->serialize() == "d1:ai0ee");
    rejected = bytes->replaceValue("a", "i10e");
    require(!rejected && rejected.error() == EditError::Limit && bytes->serialize() == "d1:ai0ee");
    auto strings = Document::parse("de", Limits{.stringBytes = 1});
    rejected = strings->replaceValue("a", "2:xx");
    require(!rejected && rejected.error() == EditError::Limit && strings->serialize() == "de");
    rejected = strings->replaceValue("long", "i0e");
    require(!rejected && rejected.error() == EditError::Limit);

    auto owned = Document::parse("de");
    require(owned->replaceValue("a", "3:abc").has_value());
    std::weak_ptr<const std::string> replaced = owned->find("a")->origin;
    require(owned->replaceValue("a", "3:def").has_value() && replaced.expired());
    require(owned->replaceValue(std::string_view("\xff", 1), "4:data").has_value());
    require(owned->find(std::string_view("\xff", 1))->bytes == "data");
    for (int i = 0; i < 1000; ++i) {
      require(owned->replaceValue("a", "i" + std::to_string(i) + "e").has_value());
      require(owned->setInteger("a", i + 1));
      require(owned->find("a")->integer == i + 1);
      require(Document::parse(owned->serialize()).has_value());
    }
    Node malformed{Node::Type::Dictionary};
    malformed.children.emplace_back(Node::Type::Integer);
    require(!encodeValue(malformed));
    auto bounded = Document::parse("d1:a4:datae");
    require(!encodeValue(bounded->root(), Limits{.inputBytes = 4}));
    require(!encodeValue(bounded->root(), Limits{.stringBytes = 3}));
    require(!encodeValue(bounded->root(), Limits{.nodes = 2}));
    require(!encodeValue(bounded->root(), Limits{.depth = 0}));
    std::cout << "PASS structured insertion/replacement, nested splicing, source ownership, unknown/info bytes and edit quotas\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
