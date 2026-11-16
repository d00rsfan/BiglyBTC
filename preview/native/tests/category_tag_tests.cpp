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
 void liveReorderingRemovalAndRecreationDoNotRecycleIds(){
  category::Roster roster;std::vector<core::tags::runtime::Text> names{u"First",u"Second"};QVERIFY(roster.observe(names));QCOMPARE(roster.entries()[2].id,3);QCOMPARE(roster.entries()[3].id,4);
  std::ranges::reverse(names);QVERIFY(roster.observe(names));QCOMPARE(roster.entries()[2].id,4);QCOMPARE(roster.entries()[3].id,3);
  names={u"Second"};QVERIFY(roster.observe(names));names.push_back(u"First");QVERIFY(roster.observe(names));QCOMPARE(roster.entries()[3].id,5);
  names.push_back(u"First");QVERIFY(!roster.observe(names));QCOMPARE(roster.entries().size(),std::size_t{4});QCOMPARE(roster.entries()[3].id,5);
  names={u"Second",u"New"};QVERIFY(roster.observe(names));QCOMPARE(roster.entries()[3].id,6);
 }
};
QTEST_GUILESS_MAIN(CategoryTagTests)
#include "category_tag_tests.moc"
