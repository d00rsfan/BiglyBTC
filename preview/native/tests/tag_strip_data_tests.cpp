// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_strip_data.hpp"
#include <QFile>
#include <QTest>
#include <algorithm>
#include <stdexcept>
using namespace biglybtc;
namespace {
std::string b(std::string_view x){return std::to_string(x.size())+":"+std::string(x);}
std::string dict(std::vector<std::pair<std::string,std::string>> x){std::sort(x.begin(),x.end());std::string s="d";for(auto& [k,v]:x){s+=b(k)+v;}return s+"e";}
std::string tag(std::string name,std::string config,std::string member){return dict({{"n",b(name)},{"c",config},{"o","l"+(member.empty()?"":b(member))+"e"}});}
core::TorrentHash hash(){std::string raw(20,'\0');for(unsigned i=0;i<20;++i){raw[i]=static_cast<char>(i);}return core::TorrentHash::parse(raw).value();}
ui::TagStripPresentation data(){const auto tags=core::readSavedDownloadTags(dict({{"3",dict({{"1",tag("Group 12",dict({{"gr",b("Group 2")},{"flag","i1e"},{"col.rgb",b("12,34,56")},{"vis","i0e"}}),"AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQT")},{"2",tag("Group 2",dict({{"gr",b("Group 2")}}),"")},{"3",tag("Loose 1","de","")}})}}));const auto cats=core::readSavedCategories(dict({{"categories","l"+dict({{"name",b("Category 10")}})+dict({{"name",b("Category 2")}})+"e"}}));if(!tags||!cats){throw std::runtime_error("Generated strip sources");}const std::array hashes{hash()};auto result=ui::makeTagStripData(5,7,11,*tags,*cats,hashes);if(!result){throw std::runtime_error("Generated strip join");}return *result;}
}
class TagStripDataTests final:public QObject {
 Q_OBJECT
private slots:
 void originalDefaultsAndAtomicPreservingSettings(){
  auto doc=core::Document::parse(dict({{"opaque",dict({{"keep",b("generated")}})},{"Library.ShowTagButtons.Inclusive","i4294967296e"},{"Library.ShowTagButtons.Align","i4294967298e"}}));QVERIFY(doc);auto original=ui::readTagStripSettings(&*doc);QVERIFY(!original.categories&&original.tags&&!original.inclusive&&!original.filtersOnly&&!original.categoriesCompletedOnly&&!original.tagsCompletedOnly);QCOMPARE(original.alignment,2);const auto before=doc->serialize();auto same=ui::editTagStripSettings(*doc,original,original);QVERIFY(same&&!*same);QCOMPARE(doc->serialize(),before);auto next=original;next.categories=true;next.tagsCompletedOnly=true;next.inclusive=true;QVERIFY(ui::editTagStripSettings(*doc,original,next));QCOMPARE(doc->find("opaque")->find("keep")->bytes,std::string_view("generated"));QCOMPARE(doc->find("Library.ShowTagButtons.Align")->integer,std::int64_t{4294967298});QVERIFY(ui::readTagStripSettings(&*doc)==next);const auto saved=doc->serialize();QVERIFY(!ui::editTagStripSettings(*doc,original,next));QCOMPARE(doc->serialize(),saved);
 }
 void sourceGroupsNaturalOrderingAndClassicVisibility(){
  auto value=data();QCOMPARE(value->chips.size(),7);QStringList names;for(const auto& chip:value->chips){names.push_back(chip.name);}QCOMPARE(names,QStringList({"All","Category 2","Category 10","Uncategorised","Group 2","Group 12","Loose 1"}));const auto& marked=value->chips[5];QVERIFY(marked.filter&&!marked.visible&&marked.colour);QCOMPARE((*marked.colour)[1],34);QCOMPARE(marked.hashes.size(),1);QVERIFY(value->membership->contains(hash()));QCOMPARE(value->membership->visibility(),core::ManualTagVisibility::All);ui::TagStripSettings settings;QVERIFY(ui::tagChipShown(marked,settings,false));settings.filtersOnly=true;QVERIFY(ui::tagChipShown(marked,settings,false));QVERIFY(!ui::tagChipShown(value->chips[4],settings,true));settings.tagsCompletedOnly=true;QVERIFY(!ui::tagChipShown(marked,settings,false));QVERIFY(ui::tagChipShown(marked,settings,true));settings.categories=true;settings.categoriesCompletedOnly=true;QVERIFY(!ui::tagChipShown(value->chips[0],settings,false));QVERIFY(ui::tagChipShown(value->chips[0],settings,true));
 }
 void originalSelectionCtrlAllEmptyUnionAndIntersection(){
  auto value=data();ui::TagStripSelection state;state.setData(value);const auto id=QString::fromStdString(hash().hex());QVERIFY(state.accepts(id,""));const auto category=value->chips[1].key,tag=value->chips[5].key;state.click(category,false);QVERIFY(state.accepts(id,"Category 2"));QVERIFY(!state.accepts(id,"Category 10"));state.click(tag,true);QCOMPARE(state.selected().size(),2);QVERIFY(state.accepts(id,"Category 10"));QVERIFY(state.accepts("other","Category 2"));state.setInclusive(false);QVERIFY(state.accepts(id,"Category 2"));QVERIFY(!state.accepts("other","Category 2"));QVERIFY(!state.accepts(id,"Category 10"));state.click(ui::AllTagChip,true);QCOMPARE(state.selected().size(),1);QVERIFY(state.accepts("other",""));state.setInclusive(true);state.click(ui::AllTagChip,false);QVERIFY(state.selected().empty());QVERIFY(!state.accepts(id,"Category 2"));state.setInclusive(false);QVERIFY(state.accepts(id,"Category 2"));state.click(value->chips[3].key,false);QVERIFY(state.accepts(id,""));QVERIFY(state.accepts(id,"undefined category"));QVERIFY(!state.accepts(id,"Category 2"));
 }
 void actualOriginalNaturalComparatorFixture(){
  QFile file(QString::fromUtf8(qgetenv("BIGLYBTC_TAG_STRIP_FIXTURE")));QVERIFY2(file.open(QIODevice::ReadOnly),"Actual original comparator fixture is required");auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);const auto* rows=doc->find("comparisons");QVERIFY(rows);for(const auto& row:rows->children){const auto a=QString::fromUtf8(row.children[0].bytes),b=QString::fromUtf8(row.children[1].bytes);const auto result=ui::originalTagNameCompare(a,b);QCOMPARE(result==0?0:result<0?-1:1,int(row.children[2].integer));}QStringList expected,actual;for(const auto& name:doc->find("generated_ui_order")->children){expected.push_back(QString::fromUtf8(name.bytes));}for(const auto& chip:data()->chips){actual.push_back(chip.name);}QCOMPARE(actual,expected);
 }
};
QTEST_GUILESS_MAIN(TagStripDataTests)
#include "tag_strip_data_tests.moc"
