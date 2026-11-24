// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/category_tags.hpp>
#include <biglybtc/download_state_names.hpp>
#include <biglybtc/bencode.hpp>
#include <QFile>
#include <algorithm>
#include <QTest>
using namespace biglybtc;namespace category=core::tags::category;
namespace {
std::u16string text(const core::Node& node){return QString::fromUtf8(node.bytes.data(),qsizetype(node.bytes.size())).toStdU16String();}
}
class CategoryTagTests final:public QObject {Q_OBJECT
private slots:
 void actualOriginalConstructorIdentitiesAndMembership(){
  QFile file(QString::fromUtf8(CATEGORY_TAG_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);unsigned comparisons=0;
  for(const auto& test:doc->find("cases")->children){
   category::Roster roster;std::vector<core::tags::runtime::Text> names;
   for(const auto& tag:test.find("tags")->children){if(tag.find("kind")->integer==0){names.push_back(text(*tag.find("raw")));}}
   QVERIFY(roster.observe(names));const auto& tags=test.find("tags")->children;QCOMPARE(tags.size(),roster.entries().size());
   const auto assigned=test.find("assigned")->integer;std::optional<category::Kind> assignedKind;
   if(assigned>=0){assignedKind=static_cast<category::Kind>(tags[std::size_t(assigned)].find("kind")->integer);}
   for(std::size_t i=0;i<tags.size();++i){const auto& source=tags[i];const auto& entry=roster.entries()[i];
    QCOMPARE(entry.id,source.find("id")->integer);QCOMPARE((std::int64_t{1}<<32)|entry.id,source.find("uid")->integer);
    QCOMPARE(entry.rawName,text(*source.find("raw")));QCOMPARE(static_cast<int>(entry.kind),int(source.find("kind")->integer));
    const auto manual=test.find("manual_member")->integer!=0;
    QCOMPARE(category::member(entry.kind,true,manual,assignedKind),bool(source.find("member")->integer));
    QCOMPARE(category::member(entry.kind,false,manual,assignedKind),bool(source.find("peer")->integer));comparisons+=2;
   }
  }QCOMPARE(comparisons,224u);
 }
 void actualOriginalCategoryLabels(){
  QFile file(QString::fromUtf8(CATEGORY_LABEL_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);unsigned comparisons=0;
  for(const auto& row:doc->find("rows")->children){auto names=core::tags::stockCategoryTagNames(row.find("locale")->bytes);QVERIFY(names);const auto& values=row.find("names")->children;QCOMPARE(values.size(),names->size());for(unsigned i=0;i<3;++i){QCOMPARE((*names)[i],text(values[i]));++comparisons;}}
  QCOMPARE(comparisons,198u);QVERIFY(!core::tags::stockCategoryTagNames(""));QVERIFY(!core::tags::stockCategoryTagNames("zz_ZZ"));
 }
 void originalStartupEnumerationAndFirstLocalizedMatch() {
  namespace runtime = core::tags::runtime;
  QFile file(QString::fromUtf8(CATEGORY_ORDER_ORIGINAL_FIXTURE));
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto fixture = core::Document::parse(file.readAll().toStdString());
  QVERIFY(fixture);
  const auto* cases = fixture->find("cases");
  QVERIFY(cases);
  QCOMPARE(cases->children.size(), std::size_t{5});
  for (const auto& row : cases->children) {
   category::Roster roster;
   std::vector<runtime::Text> names;
   for (const auto& name : row.find("names")->children) { names.push_back(text(name)); }
   QVERIFY(roster.observe(names));
   const auto& order = roster.enumeration();
   QCOMPARE(order.has_value(), row.find("linked")->integer == 1);
   if (!order) { continue; }
   const auto& original = row.find("tags")->children;
   QCOMPARE(order->size(), original.size());
   runtime::Type type{1,{},true};
   for (std::size_t index = 0; index < original.size(); ++index) {
    const auto& entry = roster.entries()[(*order)[index]];
    const auto& expected = original[index];
    QCOMPARE(entry.id, expected.find("id")->integer);
    QCOMPARE(entry.rawName, text(*expected.find("raw")));
    const auto name = entry.kind == category::Kind::All ? u"All"
        : entry.kind == category::Kind::Uncategorized ? u"Uncategorized" : entry.rawName;
    QCOMPARE(name, text(*expected.find("name")));
    runtime::Tag tag;
    tag.id = entry.id;
    tag.taggableTypes = 2;
    tag.name = name;
    type.tags.push_back(std::move(tag));
   }
   const auto registry = runtime::Registry::create({std::move(type)}, true);
   QVERIFY(registry);
   const auto first = registry->lookupName(u"All");
   QVERIFY(first && first->complete);
   QCOMPARE(first->tags.size(), std::size_t{1});
   const auto found = std::ranges::find(roster.entries(), first->tags.front().id, &category::Entry::id);
   QVERIFY(found != roster.entries().end());
   QCOMPARE(found->rawName, text(*row.find("first_all")));
  }
 }
 void originalLiveCommittedCategoryChanges() {
  QFile file(QString::fromUtf8(CATEGORY_ORDER_ORIGINAL_FIXTURE));
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto fixture = core::Document::parse(file.readAll().toStdString());
  QVERIFY(fixture);
  const auto* cases = fixture->find("lifecycle");
  QVERIFY(cases);
  QCOMPARE(cases->children.size(), std::size_t{3});
  std::size_t compared = 0;
  for (const auto& row : cases->children) {
   category::Roster roster;
   std::vector<core::tags::runtime::Text> names;
   for (const auto& name : row.find("names")->children) { names.push_back(text(name)); }
   QVERIFY(roster.observe(names));
   const auto check = [&](const core::Node& expected) {
    const auto& order = roster.enumeration();
    QCOMPARE(order.has_value(), expected.find("linked")->integer == 1);
    if (!order) { return; }
    const auto& tags = expected.find("tags")->children;
    QCOMPARE(order->size(), tags.size());
    std::optional<std::u16string> firstAll;
    for (std::size_t index = 0; index < order->size(); ++index) {
     const auto& entry = roster.entries()[(*order)[index]];
     QCOMPARE(entry.rawName, text(*tags[index].find("raw")));
     QCOMPARE(entry.id, tags[index].find("id")->integer);
     if (!firstAll && (entry.kind == category::Kind::All || entry.rawName == u"All")) {
      firstAll = entry.rawName;
     }
     ++compared;
    }
    QVERIFY(firstAll);
    QCOMPARE(*firstAll, text(*expected.find("first_all")));
   };
   check(*row.find("fresh"));
   for (const auto& step : row.find("steps")->children) {
    using Change = category::DefinitionChange;
    const auto name = text(*step.find("raw"));
    const bool add = step.find("add")->integer == 1;
    if (add) { names.push_back(name); }
    else { QCOMPARE(std::erase(names, name), std::size_t{1}); }
    // The committed on-disk order can differ from both insertion order and
    // live enumeration. An attested operation gives the exact membership delta.
    std::ranges::reverse(names);
    QVERIFY(roster.observe(names, Change{add ? Change::Operation::Added : Change::Operation::Removed, name}));
    check(step);
   }
  }
  QCOMPARE(compared, std::size_t{480});
 }
 void liveReorderingRemovalAndRecreationDoNotRecycleIds(){
  category::Roster roster;std::vector<core::tags::runtime::Text> names{u"First",u"Second"};QVERIFY(roster.observe(names));QCOMPARE(roster.entries()[2].id,3);QCOMPARE(roster.entries()[3].id,4);
  QVERIFY(roster.enumeration());QVERIFY(roster.observe(names));QVERIFY(roster.enumeration());
  std::ranges::reverse(names);QVERIFY(roster.observe(names));QCOMPARE(roster.entries()[2].id,4);QCOMPARE(roster.entries()[3].id,3);
  QVERIFY(!roster.enumeration());
  names={u"Second"};QVERIFY(roster.observe(names));names.push_back(u"First");QVERIFY(roster.observe(names));QCOMPARE(roster.entries()[3].id,5);
  names.push_back(u"First");QVERIFY(!roster.observe(names));QCOMPARE(roster.entries().size(),std::size_t{4});QCOMPARE(roster.entries()[3].id,5);
  names={u"Second",u"New"};QVERIFY(roster.observe(names));QCOMPARE(roster.entries()[3].id,6);
 }
};
QTEST_GUILESS_MAIN(CategoryTagTests)
#include "category_tag_tests.moc"
