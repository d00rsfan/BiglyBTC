#include "local_rating_dialog.hpp"
#include "../adapters/local_rating.hpp"
#include "torrent_state.hpp"
#include <openssl/evp.h>
#include <zlib.h>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
namespace storage=biglybtc::storage;
namespace core=biglybtc::core;
namespace ui=biglybtc::ui;
namespace {
std::string b(std::string_view x) { return std::to_string(x.size())+':'+std::string(x); }
std::string dictionary(std::initializer_list<std::pair<std::string,std::string>> entries) {
  std::vector<std::pair<std::string,std::string>> values(entries);
  std::sort(values.begin(),values.end()); std::string result="d";
  for(const auto& [key,value]:values) { result+=b(key)+value; } return result+'e';
}
void write(const fs::path& path,std::string_view bytes) {
  std::ofstream stream(path,std::ios::binary|std::ios::trunc);
  stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
  if(!stream) { throw std::runtime_error("owned fixture write"); }
}
std::string read(const fs::path& path) {
  std::ifstream stream(path,std::ios::binary);
  if(!stream) { throw std::runtime_error("owned fixture read"); }
  return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
std::string raw(const core::Document& doc,const core::Node& node) {
  return (node.origin?*node.origin:doc.originalBytes()).substr(node.begin,node.end-node.begin);
}
std::string gzip(std::string_view bytes) {
  z_stream stream{};
  if(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY)!=Z_OK) { throw std::runtime_error("gzip init"); }
  std::string output(compressBound(static_cast<uLong>(bytes.size()))+64,'\0');
  stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data())); stream.avail_in=static_cast<uInt>(bytes.size());
  stream.next_out=reinterpret_cast<Bytef*>(output.data()); stream.avail_out=static_cast<uInt>(output.size());
  const auto status=deflate(&stream,Z_FINISH); const auto length=stream.total_out; deflateEnd(&stream);
  if(status!=Z_STREAM_END) { throw std::runtime_error("gzip encode"); }
  output.resize(length); return output;
}
core::TorrentHash hash(std::string_view info) {
  unsigned char bytes[20]{}; unsigned length=0;
  if(EVP_Digest(info.data(),info.size(),bytes,&length,EVP_sha1(),nullptr)!=1 || length!=20) { throw std::runtime_error("hash"); }
  auto parsed=core::TorrentHash::parse(std::string_view(reinterpret_cast<char*>(bytes),20));
  if(!parsed) { throw std::runtime_error("hash parse"); } return *parsed;
}
struct Profile {
  QTemporaryDir temp;
  std::string info=dictionary({{"length","i3e"},{"name",b("fixture.bin")},{"piece length","i16384e"},{"pieces",b(std::string(20,'x'))}});
  core::TorrentHash identity=hash(info);
  std::string resume=dictionary({{"data",dictionary({{"blocks","de"},{"resume data",b(std::string("\1\0\2",3))},{"valid","i1e"}})}});
  std::string attributes=dictionary({{"Plugin.azrating.comment",b("old comment")},{"Plugin.azrating.rating",b("2")},{"future",b(std::string("x\0y",3))}});
  std::optional<storage::ProfileLease> lease;
  fs::path path() const { return temp.path().toStdString(); }
  fs::path statePath() const { return path()/"active"/(identity.hex()+".dat"); }
  void inventory(bool duplicate=false) {
    const auto bytes=identity.bytes();
    const auto row=dictionary({{"save_dir",b("/owned/payload")},{"state","i70e"},{"torrent",b("fixture.torrent")},
      {"torrent_hash",b(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()))}});
    write(path()/"downloads.config",dictionary({{"downloads","l"+row+(duplicate?row:"")+"e"}}));
  }
  Profile() {
    if(!temp.isValid()) { throw std::runtime_error("temporary profile"); }
    fs::create_directory(path()/"active"); inventory();
    write(statePath(),dictionary({{"attributes",attributes},{"future-root",b("untouched")},{"info",info},{"resume",resume}}));
    auto acquired=storage::ProfileLease::acquire(path());
    if(!acquired) { throw std::runtime_error(acquired.error().message); } lease.emplace(std::move(*acquired));
  }
  ~Profile() { lease.reset(); }
};
ui::LocalRatingRequest request() { return {9,12,15,{QString(40,QLatin1Char('a')),21}}; }
std::shared_ptr<ui::LocalRatingPacket> packet(qint32 score=2,QString comment=QStringLiteral("saved")) {
  auto values=std::make_shared<ui::LocalRatingValues>(); values->score=score; values->comment=std::move(comment);
  values->attributesRevision=QByteArray(32,'r');
  auto result=std::make_shared<ui::LocalRatingPacket>(); result->identity=request(); result->snapshot=std::move(values); return result;
}
class Hook final:public QObject {
public:
  std::function<void()> action;
  bool eventFilter(QObject*,QEvent* event) override {
    if(event->type()==QEvent::ToolTipChange && action) { auto callback=std::move(action); callback(); return true; }
    return false;
  }
};
}
class LocalRatingTests:public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    qRegisterMetaType<ui::LocalRatingRequest>(); qRegisterMetaType<ui::LocalRatingSubmission>();
  }
  void localSavePreservesInfoResumeAndUnknownFields() {
    Profile p; const auto before=read(p.statePath());
    auto first=storage::readLocalRating(*p.lease,p.identity); QVERIFY(first);
    QCOMPARE(first->local.value.score,2); QCOMPARE(*first->local.value.nick,std::u16string(u"Anonymous"));
    QCOMPARE(read(p.statePath()),before); QVERIFY(!fs::exists(p.statePath().string()+".bak"));
    auto saved=storage::saveLocalRating(*p.lease,p.identity,first->attributesRevision,5,u"Hello \U0001f30d"); QVERIFY(saved);
    QVERIFY(saved->changed); QVERIFY(saved->publicationSuggested); QVERIFY(!saved->publicationPerformed);
    auto doc=core::Document::parse(read(p.statePath())); QVERIFY(doc);
    QCOMPARE(raw(*doc,*doc->find("info")),p.info); QCOMPARE(raw(*doc,*doc->find("resume")),p.resume);
    QCOMPARE(doc->find("future-root")->bytes,std::string_view("untouched"));
    QCOMPARE(doc->find("attributes")->find("future")->bytes,std::string_view("x\0y",3));
    auto loaded=storage::readLocalRating(*p.lease,p.identity); QVERIFY(loaded);
    QCOMPARE(loaded->local.value.score,5); QCOMPARE(*loaded->local.value.comment,std::u16string(u"Hello \U0001f30d"));
    const auto unchanged=read(p.statePath());
    auto noOp=storage::saveLocalRating(*p.lease,p.identity,loaded->attributesRevision,5,*loaded->local.value.comment); QVERIFY(noOp);
    QVERIFY(!noOp->changed); QVERIFY(!noOp->publicationSuggested); QCOMPARE(read(p.statePath()),unchanged);
  }
  void cachedAttributesWinAndLeaveUnrelatedCacheRows() {
    Profile p; const auto bytes=p.identity.bytes();
    const auto key=b(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    const auto cached=dictionary({{"Plugin.azrating.comment",b("cached")},{"Plugin.azrating.rating",b("4")},{"future",b("cached unknown")}});
    const auto other=dictionary({{"hash",b(std::string(20,'z'))},{"future",b("other")}});
    const auto entry=dictionary({{"attributes",cached},{"hash",key}});
    write(p.path()/"active/cache.dat",gzip(dictionary({{"future",b("keep")},{"state","l"+entry+other+"e"}})));
    const auto cacheBefore=read(p.path()/"active/cache.dat"), stateBefore=read(p.statePath());
    auto first=storage::readLocalRating(*p.lease,p.identity); QVERIFY(first); QCOMPARE(first->local.value.score,4);
    QCOMPARE(read(p.path()/"active/cache.dat"),cacheBefore); QCOMPARE(read(p.statePath()),stateBefore);
    auto saved=storage::saveLocalRating(*p.lease,p.identity,first->attributesRevision,3,u"edited"); QVERIFY(saved);
    auto state=core::Document::parse(read(p.statePath())); QVERIFY(state);
    QCOMPARE(raw(*state,*state->find("resume")),p.resume);
    QCOMPARE(state->find("attributes")->find("future")->bytes,std::string_view("cached unknown"));
    auto cache=storage::EncodedDocument::parse(read(p.path()/"active/cache.dat"),storage::compatibleConfigLimits()); QVERIFY(cache);
    QCOMPARE(cache->document().find("state")->children.size(),std::size_t{1});
    QCOMPARE(raw(cache->document(),cache->document().find("state")->children.front()),other);
    QCOMPARE(cache->document().find("future")->bytes,std::string_view("keep"));
  }
  void staleWrongIdentityAndDuplicateTargetsRefuseBeforeSave() {
    Profile p; auto first=storage::readLocalRating(*p.lease,p.identity); QVERIFY(first);
    auto saved=storage::saveLocalRating(*p.lease,p.identity,first->attributesRevision,3,u"new"); QVERIFY(saved);
    auto before=read(p.statePath());
    auto stale=storage::saveLocalRating(*p.lease,p.identity,first->attributesRevision,4,u"stale"); QVERIFY(!stale);
    QCOMPARE(stale.error().code,storage::ErrorCode::Conflict); QCOMPARE(read(p.statePath()),before);
    p.inventory(true); QVERIFY(!storage::readLocalRating(*p.lease,p.identity));
    QCOMPARE(read(p.statePath()),before); p.inventory();
    write(p.statePath(),dictionary({{"attributes",p.attributes},{"info",dictionary({{"length","i4e"},{"name",b("changed")}})},{"resume",p.resume}}));
    before=read(p.statePath()); QVERIFY(!storage::readLocalRating(*p.lease,p.identity)); QCOMPARE(read(p.statePath()),before);
  }
  void attributeSnapshotCasRejectsChangedCacheAndKeepsResume() {
    Profile p; auto store=storage::TorrentStateStore::open(*p.lease); QVERIFY(store);
    auto state=store->loadAttributes(*p.lease,p.identity); QVERIFY(state);
    write(p.path()/"active/cache.dat",gzip(dictionary({{"state","le"}})));
    const auto before=read(p.statePath());
    auto result=store->saveAttributes(*p.lease,*state,p.attributes); QVERIFY(!result);
    QCOMPARE(result.error().code,storage::ErrorCode::Conflict); QCOMPARE(read(p.statePath()),before);
    QVERIFY(!fs::exists(p.statePath().string()+".bak"));
  }
  void noncanonicalRatingAndNicknameUseQualifiedOriginalTextRules() {
    Profile p;
    write(p.path()/"biglybt.config",dictionary({{"Plugin.azrating.nick",b(" \tNamed\r\n ")}}));
    const auto attrs=dictionary({{"Plugin.azrating.rating",b("\xd9\xa3")},{"Plugin.azrating.comment",b("\xed\xa0\x80")}});
    write(p.statePath(),dictionary({{"attributes",attrs},{"info",p.info},{"resume",p.resume}}));
    auto value=storage::readLocalRating(*p.lease,p.identity); QVERIFY(value); QCOMPARE(value->local.value.score,3);
    QCOMPARE(*value->local.value.nick,std::u16string(u"Named")); QCOMPARE(*value->local.value.comment,std::u16string(u"\ufffd"));
    const auto before=read(p.statePath());
    auto wrong=storage::saveLocalRating(*p.lease,p.identity,value->attributesRevision,6,u"no"); QVERIFY(!wrong); QCOMPARE(read(p.statePath()),before);
  }
  void dialogRequestsOnceAndSubmitsExactBoundDraft() {
    ui::LocalRatingDialog dialog(request()); QSignalSpy reads(&dialog,&ui::LocalRatingDialog::readRequested);
    QSignalSpy writes(&dialog,&ui::LocalRatingDialog::saveRequested);
    QCOMPARE(reads.size(),0); dialog.show(); QTRY_COMPARE(reads.size(),1);
    dialog.hide(); dialog.show(); QCoreApplication::processEvents(); QCOMPARE(reads.size(),1);
    auto input=packet(); dialog.setPresentation(input);
    auto alias=std::const_pointer_cast<ui::LocalRatingValues>(input->snapshot);
    alias->attributesRevision.fill('x'); alias->score=1; alias->comment=QStringLiteral("caller changed");
    auto* score=dialog.findChild<QComboBox*>("local-rating-score"); auto* comment=dialog.findChild<QPlainTextEdit*>("local-rating-comment");
    auto* save=dialog.findChild<QPushButton*>("local-rating-save"); QVERIFY(score&&comment&&save); QVERIFY(!save->isEnabled());
    score->setCurrentIndex(4); comment->setPlainText(QStringLiteral("mine")); QTRY_VERIFY(save->isEnabled()); save->click();
    QCOMPARE(writes.size(),1); const auto submitted=qvariant_cast<ui::LocalRatingSubmission>(writes.at(0).at(0));
    QVERIFY(submitted.identity==request()); QCOMPARE(submitted.attributesRevision,QByteArray(32,'r'));
    QCOMPARE(submitted.score,4); QCOMPARE(submitted.comment,QStringLiteral("mine")); QVERIFY(!save->isEnabled());
    auto result=std::make_shared<ui::LocalRatingSavePacket>(); result->identity=request(); result->saved=true;
    auto stale=std::make_shared<ui::LocalRatingSavePacket>(*result); ++stale->identity.request;
    dialog.setResult(stale); QVERIFY(dialog.isVisible()); dialog.setResult(result); QCOMPARE(dialog.result(),int(QDialog::Accepted));
  }
  void hiddenBeforeQueuedReadAndStalePacketsDoNotLoad() {
    ui::LocalRatingDialog dialog(request()); QSignalSpy reads(&dialog,&ui::LocalRatingDialog::readRequested);
    dialog.show(); dialog.hide(); QCoreApplication::processEvents(); QCOMPARE(reads.size(),0);
    auto stale=packet(); ++stale->identity.session; dialog.setPresentation(stale);
    auto* score=dialog.findChild<QComboBox*>("local-rating-score"); QVERIFY(score); QVERIFY(!score->isEnabled());
    dialog.show(); QTRY_COMPARE(reads.size(),1); dialog.setPresentation(packet(-2));
    QCOMPARE(score->currentData().toInt(),-2); QVERIFY(score->isEnabled());
    auto* comment=dialog.findChild<QPlainTextEdit*>("local-rating-comment"); QVERIFY(comment);
    comment->setPlainText(QStringLiteral("draft")); dialog.setPresentation(packet(5,QStringLiteral("late")));
    QCOMPARE(comment->toPlainText(),QStringLiteral("draft")); QCOMPARE(score->currentData().toInt(),-2);
  }
  void tooltipCallbackMayDeleteDialogOrAnIndividualControl() {
    auto* dialog=new ui::LocalRatingDialog(request()); QPointer<ui::LocalRatingDialog> weak=dialog;
    auto* status=dialog->findChild<QLabel*>("local-rating-status"); QVERIFY(status); Hook hook;
    hook.action=[&] { delete dialog; }; status->installEventFilter(&hook);
    dialog->setPresentation(packet()); QVERIFY(weak.isNull());
    ui::LocalRatingDialog surviving(request()); auto* label=surviving.findChild<QLabel*>("local-rating-status");
    Hook child; child.action=[&] { delete label; }; label->installEventFilter(&child);
    surviving.setPresentation(packet());
    auto* save=surviving.findChild<QPushButton*>("local-rating-save"); QVERIFY(save); QVERIFY(!save->isEnabled());
  }
  void synchronousSaveReceiverMayDeleteDialogAndNoReadStartsProviderAgain() {
    auto* reading=new ui::LocalRatingDialog(request()); QPointer<ui::LocalRatingDialog> readWeak=reading;
    connect(reading,&ui::LocalRatingDialog::readRequested,this,[&](const auto&) { delete reading; });
    reading->show(); QTRY_VERIFY(readWeak.isNull());
    auto* dialog=new ui::LocalRatingDialog(request()); QPointer<ui::LocalRatingDialog> weak=dialog;
    dialog->setPresentation(packet()); auto* comment=dialog->findChild<QPlainTextEdit*>("local-rating-comment");
    auto* save=dialog->findChild<QPushButton*>("local-rating-save"); QVERIFY(comment&&save);
    connect(dialog,&ui::LocalRatingDialog::saveRequested,this,[&](const auto&) { delete dialog; });
    comment->setPlainText(QStringLiteral("changed")); QTRY_VERIFY(save->isEnabled()); save->click(); QVERIFY(weak.isNull());
  }
  void failedSaveRetainsDraftAndRefusesLateReadOrAutomaticRetry() {
    ui::LocalRatingDialog dialog(request()); dialog.setPresentation(packet());
    auto* comment=dialog.findChild<QPlainTextEdit*>("local-rating-comment");
    auto* save=dialog.findChild<QPushButton*>("local-rating-save"); QVERIFY(comment&&save);
    QSignalSpy writes(&dialog,&ui::LocalRatingDialog::saveRequested);
    comment->setPlainText(QStringLiteral("keep draft")); QTRY_VERIFY(save->isEnabled()); save->click(); QCOMPARE(writes.size(),1);
    auto failure=std::make_shared<ui::LocalRatingSavePacket>(); failure->identity=request(); failure->failure=QStringLiteral("Reload after a save error");
    dialog.setResult(failure); QCOMPARE(comment->toPlainText(),QStringLiteral("keep draft")); QVERIFY(!save->isEnabled());
    dialog.setPresentation(packet(5,QStringLiteral("late"))); QCOMPARE(comment->toPlainText(),QStringLiteral("keep draft"));
    save->click(); QCOMPARE(writes.size(),1);
  }
};
QTEST_MAIN(LocalRatingTests)
#include "local_rating_tests.moc"
