// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_chip_assignment_profile.hpp"
#ifdef TAG_ASSIGNMENT_OWNER_AVAILABLE
#include "tag_assignment_owner.hpp"
#endif
#include "biglybtc/saved_download_tag_edits.hpp"
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QTest>
#include <algorithm>
#include <stdexcept>
using namespace biglybtc;
namespace {
std::string b(std::string_view x){return std::to_string(x.size())+":"+std::string(x);}
std::string dict(std::vector<std::pair<std::string,std::string>> values){std::sort(values.begin(),values.end());std::string out="d";for(const auto& [k,v]:values){out+=b(k)+v;}return out+"e";}
std::string sha1(std::string_view value){return QCryptographicHash::hash(QByteArray(value.data(),qsizetype(value.size())),QCryptographicHash::Sha1).toStdString();}
void write(QString file,std::string_view data){QFile f(file);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(data.data(),qint64(data.size()))!=qint64(data.size())){throw std::runtime_error("Generated assignment fixture write");}}
std::string read(QString file){QFile f(file);if(!f.open(QIODevice::ReadOnly)){throw std::runtime_error("Generated assignment fixture read");}return f.readAll().toStdString();}
struct Fixture {
 QTemporaryDir root;std::optional<storage::ProfileLease> lease;std::vector<core::TorrentHash> hashes;
 Fixture(){lease=storage::ProfileLease::acquire(root.path().toStdString()).value();QDir(root.path()).mkdir("active");std::string rows="l";for(unsigned i=0;i<3;++i){const auto name="generated-"+std::to_string(i)+".bin";const auto info=dict({{"length","i3e"},{"name",b(name)},{"piece length","i16384e"},{"pieces",b(sha1("abc"))}});const auto hash=core::TorrentHash::parse(sha1(info)).value();hashes.push_back(hash);write(root.path()+"/"+QString::fromStdString(name),"abc");const auto metainfo=root.path()+"/generated-"+QString::number(i)+".torrent";write(metainfo,dict({{"info",info}}));write(state(i),dict({{"info",info},{"attributes",dict({{"future",b("generated-attribute")}})},{"resume",dict({{"data",dict({{"valid","i1e"},{"resume data",b(std::string(1,'\1'))},{"blocks","de"}})}})},{"future-root",b("generated-root")}}));rows+=dict({{"torrent_hash",b(std::string(reinterpret_cast<const char*>(hash.bytes().data()),hash.bytes().size()))},{"torrent",b(metainfo.toStdString())},{"save_dir",b(root.path().toStdString())},{"save_file",b(name)},{"state","i70e"},{"persistent","i1e"}});}rows+='e';write(root.path()+"/downloads.config",dict({{"downloads",rows}}));write(root.path()+"/categories.config",dict({{"categories","l"+dict({{"name",b("Generated category")}})+"e"}}));write(root.path()+"/tag.config",dict({{"3",dict({{"1",dict({{"n",b("Generated tag")},{"o","le"},{"p","le"}})}})},{"opaque",b("generated-extension")}}));}
 QString state(std::size_t i)const{return root.path()+"/active/"+QString::fromStdString(hashes.at(i).hex())+".dat";}
 ui::TagChipAssignmentCommand command(ui::TagChipAssignmentKind kind)const{ui::TagChipAssignmentCommand c;c.session=45;c.request=1;c.kind=kind;for(const auto& hash:hashes){c.targets.push_back({QString::fromStdString(hash.hex()),7});}if(kind==ui::TagChipAssignmentKind::Manual){c.tagId=1;}else{c.category="Generated category";}return c;}
};
}
class TagChipAssignmentTests final:public QObject {
 Q_OBJECT
private slots:
#ifdef TAG_ASSIGNMENT_OWNER_AVAILABLE
 void actualMultipleSubjectsSaveOnceWithOrderedReceiptsAndNoPrefix(){
  Fixture f;write(f.root.path()+"/biglybt.config",dict({{"tagmanager.enable","i1e"}}));const auto source=dict({{"3",dict({{"1",dict({{"n",b("Generated active selection")},{"c",dict({{"pp.2","i1e"},{"pp.8","i1e"}})},{"o","le"},{"p","le"}})}})}});write(f.root.path()+"/tag.config",source);
  auto startup=storage::TagManagerStartupMode::capture(*f.lease);QVERIFY(startup);std::optional<storage::TagManagerStartupMode> mode{std::move(*startup)};auto store=storage::ManualTagStore::open(*f.lease);QVERIFY(store);auto before=(*store)->snapshot();QVERIFY(before);auto actor=storage::TagAssignmentOwner::activate(*f.lease,45,*mode);QVERIFY(actor);
  const auto inventory=read(f.root.path()+"/downloads.config");std::vector<std::string> states;for(std::size_t i=0;i<f.hashes.size();++i){states.push_back(read(f.state(i)));}
  std::vector<storage::TagAssignmentObservation> observed;for(const auto& hash:f.hashes){observed.push_back({hash,45,f.lease->sourceRevision(),70});}
  const auto generic=app::assignTagChip(*f.lease,f.command(ui::TagChipAssignmentKind::Manual));QVERIFY(!generic.failure.isEmpty());QCOMPARE(generic.changed,std::size_t{0});QCOMPARE(read(f.root.path()+"/tag.config"),source);
  auto duplicate=observed;duplicate[1]=duplicate[0];QVERIFY(!(*actor)->changeSubjects(**store,*before,1,duplicate,true,100));
  auto foreign=observed;++foreign[1].session;QVERIFY(!(*actor)->changeSubjects(**store,*before,1,foreign,true,100));
  foreign=observed;++foreign.back().sourceRevision;QVERIFY(!(*actor)->changeSubjects(**store,*before,1,foreign,true,100));
  std::vector<storage::TagAssignmentObservation> oversized(33,observed.front());QVERIFY(!(*actor)->changeSubjects(**store,*before,1,oversized,true,100));
  std::stop_source cancel;cancel.request_stop();QVERIFY(!(*actor)->changeSubjects(**store,*before,1,observed,true,100,cancel.get_token()));QCOMPARE(read(f.root.path()+"/tag.config"),source);QVERIFY(!QFile::exists(f.root.path()+"/tag.config.bak"));
  auto saved=(*actor)->changeSubjects(**store,*before,1,observed,true,100);QVERIFY(saved);QVERIFY(saved->commit.changed);QCOMPARE(saved->commit.snapshot.generation(),before->generation()+1);QCOMPARE(saved->receipts.size(),std::size_t{3});QCOMPARE(saved->commit.snapshot.tags()->tags.front().uniqueHashes.size(),std::size_t{3});QCOMPARE(read(f.root.path()+"/tag.config.bak"),source);
  for(std::size_t i=0;i<saved->receipts.size();++i){auto queued=(*actor)->next(**store,saved->commit.snapshot,saved->receipts[i]);QVERIFY(queued);QVERIFY(queued->hash==f.hashes[i]);QCOMPARE(queued->commands.values[0],core::tags::assignment::Effect::Queue);auto forced=(*actor)->next(**store,saved->commit.snapshot,saved->receipts[i]);QVERIFY(forced);QCOMPARE(forced->commands.values[0],core::tags::assignment::Effect::ForceStart);QVERIFY(saved->receipts[i].done());}
  for(auto& value:observed){value.sourceRevision=f.lease->sourceRevision();}auto noop=(*actor)->changeSubjects(**store,saved->commit.snapshot,1,observed,true,100);QVERIFY(noop&&!noop->commit.changed&&noop->receipts.empty());
  auto removed=(*actor)->changeSubjects(**store,noop->commit.snapshot,1,observed,false,100);QVERIFY(removed&&removed->commit.changed&&removed->receipts.empty());QVERIFY(removed->commit.snapshot.tags()->tags.front().uniqueHashes.empty());
  QCOMPARE(read(f.root.path()+"/downloads.config"),inventory);for(std::size_t i=0;i<f.hashes.size();++i){QCOMPARE(read(f.state(i)),states[i]);QCOMPARE(read(f.root.path()+"/generated-"+QString::number(i)+".bin"),std::string("abc"));}
  (*actor)->close();QVERIFY((*store)->close());mode.reset();f.lease.reset();QVERIFY(storage::ProfileLease::acquire(f.root.path().toStdString()));
 }
#endif
 void groupTagToggleCommitsOncePreservesSourceAndRefusesInvalidSelections(){
  Fixture f;const auto before=read(f.root.path()+"/tag.config");auto command=f.command(ui::TagChipAssignmentKind::Manual);auto added=app::assignTagChip(*f.lease,command);QVERIFY2(added.failure.isEmpty(),qPrintable(added.failure));QCOMPARE(added.applied,std::size_t{3});QCOMPARE(added.changed,std::size_t{3});QCOMPARE(read(f.root.path()+"/tag.config.bak"),before);auto doc=core::Document::parse(read(f.root.path()+"/tag.config"));QVERIFY(doc);const auto* tag=doc->find("3")->find("1");QCOMPARE(tag->find("o")->children.size(),std::size_t{3});QCOMPARE(tag->find("p")->children.size(),std::size_t{3});QCOMPARE(doc->find("opaque")->bytes,std::string_view("generated-extension"));auto removed=app::assignTagChip(*f.lease,command);QVERIFY(removed.failure.isEmpty());QCOMPARE(removed.changed,std::size_t{3});auto empty=read(f.root.path()+"/tag.config");doc=core::Document::parse(empty);QVERIFY(doc->find("3")->find("1")->find("o")->children.empty());QVERIFY(doc->find("3")->find("1")->find("p")->children.empty());command.targets.push_back(command.targets.front());auto refused=app::assignTagChip(*f.lease,command);QVERIFY(!refused.failure.isEmpty()&&!refused.applied);QCOMPARE(read(f.root.path()+"/tag.config"),empty);command.targets.pop_back();std::stop_source stop;stop.request_stop();refused=app::assignTagChip(*f.lease,command,stop.get_token());QVERIFY(!refused.failure.isEmpty());QCOMPARE(read(f.root.path()+"/tag.config"),empty);
 }
 void batchCoreMatchesOriginalSingleMemberBytesAndKeepsMemberProperties(){
  Fixture f;const auto source=dict({{"3",dict({{"1",dict({{"n",b("Generated tag")},{"o","le"},{"p","le"},{"unknown",b("generated")}})}})}});auto sequential=source;for(const auto& hash:f.hashes){auto edited=core::editSavedDownloadTags(sequential,core::SavedTagAddMember{1,hash,123});QVERIFY(edited);sequential=edited->value.source->originalBytes();}auto batch=core::editSavedDownloadTags(source,core::SavedTagMemberBatch{1,f.hashes,true,123});QVERIFY(batch&&batch->changed);QCOMPARE(batch->value.source->originalBytes(),sequential);auto doc=core::Document::parse(sequential);auto record=core::Document::parse(core::encodeValue(*doc->find("3")->find("1")).value());QVERIFY(record->replaceValue("p","ld1:ai123e6:opaque9:generateded1:ai123eed1:ai123eee"));auto type=core::Document::parse("de");QVERIFY(type->replaceValue("1",record->serialize()));QVERIFY(doc->replaceValue("3",type->serialize()));auto removed=core::editSavedDownloadTags(doc->serialize(),core::SavedTagMemberBatch{1,{f.hashes[1]},false,0});QVERIFY(removed);const auto* props=removed->value.source->find("3")->find("1")->find("p");QCOMPARE(props->children.size(),std::size_t{2});QCOMPARE(props->children[0].find("opaque")->bytes,std::string_view("generated"));auto bad=f.hashes;bad.push_back(f.hashes[0]);QVERIFY(!core::editSavedDownloadTags(source,core::SavedTagMemberBatch{1,bad,true,123}));
 }
 void categoryGroupRebasesChangedOwnersOnceAndRetainsOriginalResumeAndPayload(){
  Fixture f;const auto inventory=read(f.root.path()+"/downloads.config");std::vector<std::string> resume;for(std::size_t i=0;i<3;++i){auto doc=core::Document::parse(read(f.state(i)));resume.push_back(core::encodeValue(*doc->find("resume")).value());}auto command=f.command(ui::TagChipAssignmentKind::Category);unsigned rebase=0;auto apply=[&](const auto& c){return app::assignTagChip(*f.lease,c,{},[&](const auto&)->std::expected<void,storage::Error>{++rebase;return {};});};auto saved=apply(command);QVERIFY2(saved.failure.isEmpty(),qPrintable(saved.failure));QCOMPARE(saved.changed,std::size_t{3});QCOMPARE(rebase,3U);for(std::size_t i=0;i<3;++i){auto doc=core::Document::parse(read(f.state(i)));QCOMPARE(doc->find("attributes")->find("category")->bytes,std::string_view("Generated category"));QCOMPARE(core::encodeValue(*doc->find("resume")).value(),resume[i]);QCOMPARE(doc->find("attributes")->find("future")->bytes,std::string_view("generated-attribute"));QCOMPARE(read(f.root.path()+"/generated-"+QString::number(i)+".bin"),std::string("abc"));}QCOMPARE(read(f.root.path()+"/downloads.config"),inventory);saved=apply(command);QVERIFY(saved.failure.isEmpty());QCOMPARE(saved.changed,std::size_t{0});QCOMPARE(rebase,3U);command.category.reset();saved=apply(command);QVERIFY(saved.failure.isEmpty());QCOMPARE(saved.changed,std::size_t{3});QCOMPARE(rebase,6U);
  command.category="Generated category";saved=app::assignTagChip(*f.lease,command,{},[](const auto&)->std::expected<void,storage::Error>{return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Generated interruption after acknowledged write"});});QVERIFY(!saved.failure.isEmpty());QCOMPARE(saved.applied,std::size_t{1});QCOMPARE(saved.changed,std::size_t{1});auto untouched=core::Document::parse(read(f.state(1)));QVERIFY(!untouched->find("attributes")->find("category"));
 }
};
QTEST_GUILESS_MAIN(TagChipAssignmentTests)
#include "tag_chip_assignment_tests.moc"
