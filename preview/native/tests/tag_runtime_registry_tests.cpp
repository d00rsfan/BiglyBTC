// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_runtime_registry.hpp>
#include <biglybtc/tag_constraints.hpp>
#include <biglybtc/bencode.hpp>
#include <QTest>
#include <QFile>
using namespace biglybtc;namespace registry=core::tags::runtime;
namespace {
registry::Text text(const core::Node& value){return QString::fromUtf8(value.bytes.data(),qsizetype(value.bytes.size())).toStdU16String();}
std::vector<std::int64_t> uids(const registry::Selection& selection){std::vector<std::int64_t> out;for(const auto& ref:selection.tags){out.push_back(ref.uid());}return out;}
std::vector<std::int64_t> values(const core::Node& node){std::vector<std::int64_t> out;for(const auto& n:node.children){out.push_back(n.integer);}return out;}
registry::Tag tag(int id,std::optional<registry::Text> name,registry::Membership membership){return {id,2,std::move(name),{},true,membership};}
}
class TagRuntimeRegistryTests final:public QObject{
 Q_OBJECT
private slots:
 void actualOriginalLookupAndTaggableUnion(){QFile file(QString::fromUtf8(TAG_REGISTRY_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));auto original=core::Document::parse(file.readAll().toStdString());QVERIFY(original);const auto* cases=original->find("cases");QVERIFY(cases);QCOMPARE(cases->children.size(),std::size_t{2});unsigned queries=0,memberships=0;
  for(const auto& row:cases->children){for(const auto target:{4,2,6}){std::vector<registry::Type> types;
   for(const auto& type:row.find("types")->children){registry::Type next;next.id=std::int32_t(type.find("type")->integer);next.complete=true;for(const auto& entry:type.find("entries")->children){const bool member=target!=6&&entry.find(target==2?"download_member":"peer_member")->integer!=0;auto item=tag(int(entry.find("id")->integer),text(*entry.find("name")),member?registry::Membership::Present:registry::Membership::Absent);item.taggableTypes=int(entry.find("taggable_type")->integer);next.tags.push_back(std::move(item));}types.push_back(std::move(next));}
   auto made=registry::Registry::create(std::move(types),true);QVERIFY(made);QCOMPARE(made->tagCount(),std::size_t{15});
   for(const auto& query:row.find("queries")->children){auto selected=made->lookupName(text(*query.find("name")));QVERIFY(selected&&selected->complete);QCOMPARE(uids(*selected),values(*query.find("matches")));for(const auto& ref:selected->tags){QVERIFY(made->at(ref));}++queries;}
   auto selected=made->tagged(target);QVERIFY(selected&&selected->complete);QCOMPARE(uids(*selected),values(*row.find(target==2?"download":target==4?"peer":"combined")));++memberships;
  }}QCOMPARE(queries,72u);QCOMPARE(memberships,6u);
 }
 void partialCensusRetainsPositiveWitnessesAndNeverInventsAbsence(){auto certain=tag(1,u"Shared",registry::Membership::Present);certain.group=u"Group";auto other=tag(2,u"Shared",registry::Membership::Unknown);auto absent=tag(3,u"Absent here",registry::Membership::Absent);auto made=registry::Registry::create({{3,{certain,other,absent},true}},false);QVERIFY(made);QCOMPARE(made->hasName(u"Shared",2),registry::Truth::Yes);QCOMPARE(made->hasGroup(u"Group",2),registry::Truth::Yes);QCOMPARE(made->hasName(u"Absent here",2),registry::Truth::Unknown);QCOMPARE(made->hasName(u"Unobserved type",2),registry::Truth::Unknown);auto lookup=made->lookupName(u"Shared");QVERIFY(lookup&&!lookup->complete);QCOMPARE(lookup->tags.size(),std::size_t{1});QCOMPARE(lookup->tags[0].id,1);auto present=made->tagged(2);QVERIFY(present&&!present->complete);QCOMPARE(present->tags.size(),std::size_t{1});}
 void completeSourcesStillRespectUnknownMetadataAndExactTypeEquality(){auto absent=tag(1,u"Name",registry::Membership::Absent);auto closed=registry::Registry::create({{3,{absent},true}},true);QVERIFY(closed);QCOMPARE(closed->hasName(u"Name",2),registry::Truth::No);auto overlap=tag(2,u"Name",registry::Membership::Present);overlap.taggableTypes=6;auto exact=registry::Registry::create({{3,{overlap},true}},true);QVERIFY(exact);QCOMPARE(exact->hasName(u"Name",2),registry::Truth::No);QCOMPARE(exact->hasName(u"Name",6),registry::Truth::Yes);auto hidden=tag(3,{},registry::Membership::Present);hidden.group=u"Known group";auto uncertain=registry::Registry::create({{3,{hidden},true}},true);QVERIFY(uncertain);QCOMPARE(uncertain->hasName(u"Anything",2),registry::Truth::Unknown);QCOMPARE(uncertain->hasGroup(u"Known group",2),registry::Truth::Yes);hidden.group.reset();hidden.groupKnown=false;auto unknownGroup=registry::Registry::create({{3,{hidden},true}},true);QVERIFY(unknownGroup);QCOMPARE(unknownGroup->hasGroup(u"Anything",2),registry::Truth::Unknown);}
 void unknownEarlierNameCannotCertifyFirstLookupButLaterUnknownCannotSupersedeIt(){const auto known=tag(1,u"Name",registry::Membership::Present),unknown=tag(2,{},registry::Membership::Unknown);auto before=registry::Registry::create({{3,{unknown,known},true}},true);auto after=registry::Registry::create({{3,{known,unknown},true}},true);QVERIFY(before&&after);QVERIFY(!before->lookupName(u"Name")->complete);QVERIFY(after->lookupName(u"Name")->complete);QVERIFY(!after->lookupName(u"Missing")->complete);QCOMPARE(before->hasName(u"Name",2),registry::Truth::Yes);}
 void sourceCopiesAreImmutableAndInvalidIdentitiesAreRefused(){auto input=std::vector<registry::Type>{{3,{tag(1,u"Before",registry::Membership::Present)},true}};auto made=registry::Registry::create(input,true);QVERIFY(made);auto retained=*made;input[0].tags[0].name=u"After";QCOMPARE(retained.hasName(u"Before",2),registry::Truth::Yes);QCOMPARE(retained.hasName(u"After",2),registry::Truth::No);QVERIFY(!registry::Registry::create({input[0],input[0]},true));input[0].tags.push_back(input[0].tags[0]);QVERIFY(!registry::Registry::create(input,true));QVERIFY(!registry::Registry::create({{0,{},true}},true));registry::Registry empty;QCOMPARE(empty.hasName(u"Name",2),registry::Truth::Unknown);QVERIFY(!empty.at({}));}
 void evaluatorUsesPositiveUnionAndRetainsUnknownSources(){namespace c=core::tags::constraints;auto first=tag(1,u"Shared",registry::Membership::Absent);auto second=tag(2,u"Shared",registry::Membership::Present);second.group=u"Group";auto pending=tag(3,u"Pending",registry::Membership::Unknown);auto made=registry::Registry::create({{3,{first,second,pending},false}},false);QVERIFY(made);c::Facts facts;facts.tagRegistry=*made;auto positive=c::compile(u"hasTag(\"Shared\")&&hasTagGroup(\"Group\")");QVERIFY(positive);auto value=c::evaluate(*positive,facts);QVERIFY(value&&c::truth(*value).value());auto unknown=c::compile(u"!hasTag(\"Pending\")");QVERIFY(unknown);auto kept=c::evaluate(*unknown,facts);QVERIFY(!kept&&kept.error().kind==c::ProblemKind::Unavailable);QCOMPARE(c::project(kept,{},true,false,true).membership,c::Membership::Keep);auto selected=made->lookupName(u"Shared");QVERIFY(selected);QCOMPARE(selected->tags[0].id,1);QCOMPARE(made->hasName(u"Shared",2),registry::Truth::Yes);}
 void evaluatorCountsRegistryResourcesAndCanUseCompleteKnownAbsence(){namespace c=core::tags::constraints;auto made=registry::Registry::create({{3,{tag(1,u"Name",registry::Membership::Absent)},true}},true);QVERIFY(made);c::Facts facts;facts.tagRegistry=*made;auto expression=c::compile(u"hasTag(\"Name\")");QVERIFY(expression);auto value=c::evaluate(*expression,facts);QVERIFY(value&&!c::truth(*value).value());c::Limits limits;limits.totalFactUnits=3;auto bounded=c::compile(u"true",limits);QVERIFY(bounded);auto refused=c::evaluate(*bounded,facts);QVERIFY(!refused&&refused.error().kind==c::ProblemKind::Bounds);}
 void tagNamesUsesDirectMembershipAndBlanksOnlyTheSelectedTag(){
  namespace c=core::tags::constraints;
  using registry::Membership;
  auto peer=tag(1,u"Peer only",Membership::Present);peer.taggableTypes=4;
  auto swarm=tag(1,u"Swarm only",Membership::Unknown);swarm.taggableTypes=0;
  const std::vector<registry::Type> types{
   {1,{tag(1,u"Uncategorized",Membership::Present)},true},
   {2,{tag(1,u"Seeding",Membership::Present)},true},
   {4,{peer},false},
   {3,{tag(0,u"Own rule",Membership::Present),tag(1,u"Same",Membership::Present),
       tag(2,u"Same",Membership::Present),tag(3,u"Absent",Membership::Absent)},true},
   {5,{},true},
   {7,{swarm},false},
  };
  auto made=registry::Registry::create(types,false);QVERIFY(made);
  auto selected=made->downloadConstraintTags();QVERIFY(selected&&selected->complete);
  const std::vector<std::int64_t> expected{0x100000001,0x200000001,0x300000000,0x300000001,0x300000002};
  QCOMPARE(uids(*selected),expected);

  c::Facts facts;
  facts.tagRegistry=*made;
  facts.downloadTagCensus=true;
  facts.selectedManualTag=0;
  facts.peerSetTagNames={u"Peer only"};
  const auto names=c::compile(u"ifThenElse(1,tag_names,tagnames)");QVERIFY(names);
  auto value=c::evaluate(*names,facts);QVERIFY(value);
  const c::Strings first{u"Uncategorized",u"Seeding",u"",u"Same",u"Same"};
  QCOMPARE(std::get<c::Strings>(*value),first);
  facts.selectedManualTag=1;
  value=c::evaluate(*names,facts);QVERIFY(value);
  const c::Strings next{u"Uncategorized",u"Seeding",u"Own rule",u"",u"Same"};
  QCOMPARE(std::get<c::Strings>(*value),next);
  facts.selectedManualTag=3;
  value=c::evaluate(*names,facts);QVERIFY(value);
  const c::Strings absent{u"Uncategorized",u"Seeding",u"Own rule",u"Same",u"Same"};
  QCOMPARE(std::get<c::Strings>(*value),absent);
  const auto firstNameLength=c::compile(u"length(tag_names)");QVERIFY(firstNameLength);
  value=c::evaluate(*firstNameLength,facts);
  QVERIFY(!value&&value.error().kind==c::ProblemKind::Unavailable);

  auto partial=types;
  partial[3].tags[0].membership=Membership::Unknown;
  made=registry::Registry::create(partial,false);QVERIFY(made);
  facts.tagRegistry=*made;
  const auto negative=c::compile(u"!contains(tagnames,\"Missing\")");QVERIFY(negative);
  value=c::evaluate(*negative,facts);
  QVERIFY(!value&&value.error().kind==c::ProblemKind::Unavailable);
  QCOMPARE(c::project(value,{},true,false,true).membership,c::Membership::Keep);
  const auto positive=c::compile(u"contains(tag_names,\"Same\")");QVERIFY(positive);
  value=c::evaluate(*positive,facts);
  QVERIFY(value&&c::truth(*value).value());
  value=c::evaluate(*names,facts);
  QVERIFY(!value&&value.error().kind==c::ProblemKind::Unavailable);

  partial=types;
  partial[3].tags[1].name.reset();
  made=registry::Registry::create(partial,false);QVERIFY(made);
  facts.tagRegistry=*made;
  value=c::evaluate(*negative,facts);
  QVERIFY(!value&&value.error().kind==c::ProblemKind::Unavailable);
  facts.selectedManualTag=1;
  value=c::evaluate(*negative,facts);
  QVERIFY(value&&c::truth(*value).value());
 }
 void resourceBoundsFailWithoutTruncatingSources(){registry::Limits limits;limits.tags=1;QVERIFY(!registry::Registry::create({{3,{tag(1,u"One",registry::Membership::Present),tag(2,u"Two",registry::Membership::Present)},true}},true,limits));limits={};limits.nameUnits=3;QVERIFY(!registry::Registry::create({{3,{tag(1,u"Four",registry::Membership::Present)},true}},true,limits));limits={};limits.totalUnits=5;QVERIFY(!registry::Registry::create({{3,{tag(1,u"One",registry::Membership::Present),tag(2,u"Two",registry::Membership::Present)},true}},true,limits));}
};
QTEST_GUILESS_MAIN(TagRuntimeRegistryTests)
#include "tag_runtime_registry_tests.moc"
