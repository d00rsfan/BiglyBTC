// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_attributes.hpp"
#include "plugin_settings.hpp"
#include <cstdlib>
#include <iostream>
using namespace biglybtc::storage;
using biglybtc::core::Document;
void require(bool value) { if (!value) { std::abort(); } }
int main() {
  auto primary = Document::parse("d10:attributesd25:Plugin.xmwebui.xmui.dl.idi9eee");
  auto emptyCache = Document::parse("d10:attributesdee");
  auto missingCache = Document::parse("de");
  auto wrongCache = Document::parse("d10:attributeslee");
  auto rootList = Document::parse("le");
  auto wrongValue = Document::parse("d10:attributesd25:Plugin.xmwebui.xmui.dl.id1:xee");
  require(primary && emptyCache && missingCache && wrongCache && rootList && wrongValue);
  const auto cached = readOriginalPluginLongAttribute(*primary, &emptyCache->root(), "xmwebui", "xmui.dl.id");
  require(cached && cached->value == 0 && cached->source == AttributeSource::Cache);
  require(readOriginalPluginLongAttribute(*primary, &missingCache->root(), "xmwebui", "xmui.dl.id")->value == 9);
  require(readOriginalPluginLongAttribute(*primary, &wrongCache->root(), "xmwebui", "xmui.dl.id").error() == AttributeError::CachedMapType);
  require(readOriginalPluginLongAttribute(*rootList, nullptr, "xmwebui", "xmui.dl.id").error() == AttributeError::Root);
  require(readOriginalPluginLongAttribute(*primary, &rootList->root(), "xmwebui", "xmui.dl.id").error() == AttributeError::CacheRoot);
  require(readOriginalPluginLongAttribute(*wrongCache, nullptr, "xmwebui", "xmui.dl.id")->source == AttributeSource::Default);
  require(readOriginalPluginLongAttribute(*wrongValue, nullptr, "xmwebui", "xmui.dl.id").error() == AttributeError::LongType);
  require(readOriginalPluginLongAttribute(*primary, nullptr, "", "xmui.dl.id").error() == AttributeError::Binding);
  require(readOriginalPluginLongAttribute(*primary, nullptr, "xmwebui", std::string(4097, 'x')).error() == AttributeError::Binding);
  auto config = Document::parse("d13:Plugin.x.longi4294967296ee");
  require(config.has_value());
  auto binding = originalPluginBinding("x", "x");
  OriginalPluginSettings settings(*config, std::move(*binding));
  require(settings.longInteger("long", 7) == 4294967296LL && settings.integer("long", 7) == 0);
  require(!settings.boolean("long", true) && settings.wrongTypes().empty());
  std::cout << "PASS original attribute cache precedence, explicit typed errors, bounds and full64/int32 conversion\n";
}
