#include "file_policy_commands.hpp"
#include "encoded_document.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <zlib.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace {
using namespace biglybtc;
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
void write(const QString& path, std::string_view value) {
  require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create owned file parent");
  QFile file(path); require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write owned fixture");
  require(file.write(value.data(), static_cast<qint64>(value.size())) == static_cast<qint64>(value.size()) && file.flush(), "Owned fixture write failed");
}
std::string read(const QString& path) {
  QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read owned fixture"); return file.readAll().toStdString();
}
core::Document parse(std::string data) {
  auto result = core::Document::parse(std::move(data), storage::compatibleConfigLimits());
  require(result.has_value(), "Cannot parse owned fixture"); return std::move(*result);
}
std::string encoded(const core::Node& node) {
  auto result = core::encodeValue(node, storage::compatibleConfigLimits());
  require(result.has_value(), "Cannot own fixture subtree"); return std::move(*result);
}
std::string gzip(std::string_view input) {
  z_stream stream{};
  require(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) == Z_OK, "Cannot initialize owned gzip");
  std::string result(compressBound(static_cast<uLong>(input.size())) + 64, '\0');
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data())); stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = reinterpret_cast<Bytef*>(result.data()); stream.avail_out = static_cast<uInt>(result.size());
  const int status = deflate(&stream, Z_FINISH);
  const auto size = stream.total_out;
  deflateEnd(&stream);
  require(status == Z_STREAM_END, "Cannot compress owned state"); result.resize(static_cast<std::size_t>(size)); return result;
}
struct Stamp {
  std::string bytes;
  struct stat metadata{};
};
Stamp stamp(const QString& path) {
  Stamp result{read(path)};
  require(::stat(path.toStdString().c_str(), &result.metadata) == 0, "Cannot stat owned fixture"); return result;
}
bool unchanged(const QString& path, const Stamp& before) {
  struct stat after{};
  return ::stat(path.toStdString().c_str(), &after) == 0 && after.st_dev == before.metadata.st_dev
      && after.st_ino == before.metadata.st_ino && after.st_size == before.metadata.st_size
      && after.st_mtim.tv_sec == before.metadata.st_mtim.tv_sec && after.st_mtim.tv_nsec == before.metadata.st_mtim.tv_nsec
      && read(path) == before.bytes;
}
class Profile final {
public:
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-file-policy-XXXXXX")};
  QString profile, state;
  std::optional<core::TorrentHash> hash;
  std::vector<QString> payloads;
  int count;
  Profile(int files = 3, bool padding = false, bool compressed = false) : count(files) {
    require(root.isValid(), "Cannot create owned priority fixture");
    profile = root.path() + QStringLiteral("/profile");
    const auto payload = root.path() + QStringLiteral("/payload");
    std::string entries = "l", priorities = "l";
    for (int i = 0; i < files; ++i) {
      const auto name = std::to_string(i) + ".bin";
      entries += "d6:lengthi4096e4:pathl" + bytes(name) + 'e';
      if (padding && i + 1 == files) { entries += "4:attr1:p"; }
      entries += 'e';
      priorities += padding && i + 1 == files ? "i0e" : "i-1e";
      payloads.push_back(payload + QStringLiteral("/bundle/") + QString::fromStdString(name));
      write(payloads.back(), std::string(4096, static_cast<char>('a' + i)));
    }
    entries += 'e'; priorities += 'e';
    const auto pieces = (files * 4096 + 16383) / 16384;
    const auto info = "d4:name6:bundle12:piece lengthi16384e5:files" + entries
        + "6:pieces" + bytes(std::string(static_cast<std::size_t>(pieces) * 20, '\0')) + 'e';
    const auto digest = QCryptographicHash::hash(QByteArray::fromStdString(info), QCryptographicHash::Sha1);
    auto parsed = core::TorrentHash::parse(std::string_view(digest.constData(), static_cast<std::size_t>(digest.size())));
    require(parsed.has_value(), "Cannot bind generated metadata hash"); hash = *parsed;
    state = profile + QStringLiteral("/active/") + QString::fromStdString(hash->hex()) + QStringLiteral(".dat");
    const auto opaque = bytes(std::string_view("\0\xffopaque", 8));
    const auto source = "d4:info" + info + "13:opaque.source" + opaque
        + "10:attributesd14:resumecompletei2e13:opaque.nestedi91ee"
        + "6:resumed4:datad11:resume data" + bytes(std::string(static_cast<std::size_t>(pieces), '\1'))
        + "5:validi1e6:blocksd1:ai1eee13:opaque.resumei7eee";
    write(state, compressed ? gzip(source) : source);
    const auto entry = "d12:torrent_hash" + bytes(digest.toStdString())
        + "7:torrent" + bytes(state.toStdString()) + "8:save_dir" + bytes(payload.toStdString())
        + "9:save_file6:bundle10:persistenti1e5:statei70e8:positioni3e15:file_priorities" + priorities
        + "12:opaque.entry" + opaque + 'e';
    write(profile + QStringLiteral("/downloads.config"), "d9:downloadsl" + entry + "e11:opaque.rooti123ee");
  }
  storage::ProfileLease hold() const {
    auto result = storage::ProfileLease::acquire(profile.toStdString());
    require(result.has_value(), "Cannot hold owned priority profile"); return std::move(*result);
  }
  core::Document inventory() const { return parse(read(profile + QStringLiteral("/downloads.config"))); }
  void replaceEntry(std::string data, bool duplicate = false) {
    auto config = inventory();
    require(config.replaceValue("downloads", "l" + data + (duplicate ? data : std::string{}) + 'e').has_value(), "Cannot rewrite owned entry");
    write(profile + QStringLiteral("/downloads.config"), config.serialize());
  }
};
const core::Node& required(const core::Node& node, std::string_view key) {
  const auto* value = node.find(key); require(value != nullptr, "Expected retained saved field"); return *value;
}
const core::Node& entry(const core::Document& doc) {
  const auto& rows = required(doc.root(), "downloads");
  require(rows.type == core::Node::Type::List && !rows.children.empty(), "Expected saved download list");
  return rows.children.at(0);
}
} // namespace

class FilePolicyCommandsTests final : public QObject {
  Q_OBJECT
private slots:
  void changedPoliciesRoundTripAndInvalidateResume_data() {
    QTest::addColumn<bool>("compressed");
    QTest::newRow("plain-state") << false; QTest::newRow("gzip-state") << true;
  }
  void changedPoliciesRoundTripAndInvalidateResume() {
    QFETCH(bool, compressed);
    Profile area(3, false, compressed);
    auto lease = area.hold();
    const auto before = area.inventory();
    const auto stateBefore = storage::EncodedDocument::parse(read(area.state), storage::compatibleConfigLimits()); QVERIFY(stateBefore);
    std::vector<Stamp> payload;
    for (const auto& path : area.payloads) { payload.push_back(stamp(path)); }
    const std::vector<compat::FilePolicy> requested{{false, 1}, {false, -1}, {true, 0}};
    QVERIFY(storage::setFilePolicies(lease, *area.hash, requested));
    const auto after = area.inventory();
    QVERIFY(entry(after).find("file_priorities_c"));
    const auto decoded = compat::originalFilePolicy(entry(after), 3); QVERIFY(decoded);
    QVERIFY(*decoded == requested);
    QCOMPARE(encoded(required(after.root(), "opaque.root")), encoded(required(before.root(), "opaque.root")));
    QCOMPARE(encoded(required(entry(after), "opaque.entry")), encoded(required(entry(before), "opaque.entry")));
    QCOMPARE(encoded(required(entry(after), "file_priorities")), encoded(required(entry(before), "file_priorities")));
    const auto stateAfter = storage::EncodedDocument::parse(read(area.state), storage::compatibleConfigLimits()); QVERIFY(stateAfter);
    QCOMPARE(stateAfter->compressed(), compressed);
    QCOMPARE(encoded(required(stateAfter->document().root(), "info")), encoded(required(stateBefore->document().root(), "info")));
    QCOMPARE(encoded(required(stateAfter->document().root(), "opaque.source")), encoded(required(stateBefore->document().root(), "opaque.source")));
    const auto* resume = stateAfter->document().find("resume"); QVERIFY(resume);
    const auto* data = resume->find("data"); QVERIFY(data);
    const auto* flags = data->find("resume data"); QVERIFY(flags);
    QCOMPARE(flags->type, core::Node::Type::Bytes);
    QCOMPARE(std::string(flags->bytes), std::string(1, '\2'));
    QVERIFY(data->find("valid")); QCOMPARE(data->find("valid")->type, core::Node::Type::Integer);
    QCOMPARE(data->find("valid")->integer, std::int64_t{0});
    QVERIFY(data->find("blocks")); QCOMPARE(data->find("blocks")->type, core::Node::Type::Dictionary);
    QVERIFY(data->find("blocks")->children.empty());
    QCOMPARE(encoded(required(*resume, "opaque.resume")), encoded(required(required(stateBefore->document().root(), "resume"), "opaque.resume")));
    QCOMPARE(required(required(stateAfter->document().root(), "attributes"), "resumecomplete").integer, std::int64_t{1});
    for (std::size_t i = 0; i < payload.size(); ++i) { QVERIFY(unchanged(area.payloads[i], payload[i])); }
  }
  void identicalPolicyIsAnActualNoWrite() {
    Profile area;
    auto lease = area.hold();
    const auto configPath = area.profile + QStringLiteral("/downloads.config");
    const auto config = stamp(configPath), state = stamp(area.state);
    const std::vector<compat::FilePolicy> normal(3);
    QVERIFY(storage::setFilePolicies(lease, *area.hash, normal));
    QVERIFY(unchanged(configPath, config)); QVERIFY(unchanged(area.state, state));
    QVERIFY(!QFileInfo::exists(configPath + QStringLiteral(".bak")));
    QVERIFY(!QFileInfo::exists(area.state + QStringLiteral(".bak")));
    QVERIFY(!QFileInfo::exists(configPath + QStringLiteral(".saving")));
  }
  void invalidPolicyRejectsBeforeAnyWrite_data() {
    QTest::addColumn<QByteArray>("kind");
    for (const auto* name : {"wrong-count", "duplicate-download", "padding-enabled", "malformed-priorities",
        "eight-ranks", "minimum-priority", "skipped-nonzero-priority"}) { QTest::newRow(name) << QByteArray(name); }
  }
  void invalidPolicyRejectsBeforeAnyWrite() {
    QFETCH(QByteArray, kind);
    Profile area(kind == "eight-ranks" ? 8 : 3, kind == "padding-enabled");
    std::vector<compat::FilePolicy> requested(static_cast<std::size_t>(area.count));
    if (kind == "wrong-count") { requested.pop_back(); }
    if (kind == "duplicate-download") { const auto config = area.inventory(); area.replaceEntry(encoded(entry(config)), true); }
    if (kind == "malformed-priorities") {
      const auto config = area.inventory(); auto value = parse(encoded(entry(config)));
      QVERIFY(value.replaceValue("file_priorities_c", "d1:11:01:21:0e")); area.replaceEntry(value.serialize());
    }
    if (kind == "eight-ranks") {
      for (std::size_t i = 0; i < requested.size(); ++i) { requested[i].priority = static_cast<std::int32_t>(i); }
    }
    if (kind == "minimum-priority") { requested.front().priority = std::numeric_limits<std::int32_t>::min(); }
    if (kind == "skipped-nonzero-priority") { requested.front() = {true, 1}; }
    auto lease = area.hold();
    const auto configPath = area.profile + QStringLiteral("/downloads.config");
    const auto config = stamp(configPath), state = stamp(area.state);
    const auto rejected = storage::setFilePolicies(lease, *area.hash, requested);
    QVERIFY(!rejected);
    if (kind == "duplicate-download") { QCOMPARE(rejected.error().code, storage::ErrorCode::Conflict); }
    QVERIFY(unchanged(configPath, config)); QVERIFY(unchanged(area.state, state));
    QVERIFY(!QFileInfo::exists(configPath + QStringLiteral(".bak")));
    QVERIFY(!QFileInfo::exists(area.state + QStringLiteral(".bak")));
    QVERIFY(!QFileInfo::exists(configPath + QStringLiteral(".saving")));
  }
};
QTEST_GUILESS_MAIN(FilePolicyCommandsTests)
#include "file_policy_commands_tests.moc"
