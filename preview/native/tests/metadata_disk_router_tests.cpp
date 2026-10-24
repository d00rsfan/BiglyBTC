#include "metadata_disk_router.hpp"
#include "owned_engine_fixture.hpp"

#include <QtTest>
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/peer_request.hpp>
#include <libtorrent/performance_counters.hpp>
#include <libtorrent/session_params.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace biglybtc;
namespace lt = libtorrent;
using Policy = engine::detail::MetadataDiskPolicy;
struct DirectDisk {
  lt::io_context context;
  lt::settings_pack settings;
  lt::counters counters;
  std::unique_ptr<lt::disk_interface> disk;
  explicit DirectDisk(const std::shared_ptr<Policy>& policy)
      : disk(engine::detail::metadataDiskConstructor(policy)(context, settings, counters)) {}
  ~DirectDisk() { disk->abort(true); context.restart(); context.poll(); }
  void pump() { context.restart(); context.poll(); }
};
struct Geometry {
  lt::file_storage files;
  lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities;
  lt::sha1_hash hash;
  Geometry() {
    files.add_file("payload.bin", 16384); files.set_piece_length(16384); files.set_num_pieces(1);
    priorities.push_back(lt::download_priority_t{4});
  }
  lt::storage_params parameters(const std::string& path) const {
    return {files, nullptr, path, lt::storage_mode_sparse, priorities, hash};
  }
};
std::uint32_t index(const lt::storage_holder& holder) {
  return static_cast<std::uint32_t>(static_cast<lt::storage_index_t>(holder));
}
lt::torrent_handle add(lt::session& session, const test::OwnedEngineFixture& fixture, const QString& path) {
  const auto raw = test::read(fixture.stateFile); lt::error_code error;
  auto metadata = std::make_shared<lt::torrent_info>(
      lt::span<const char>(raw.constData(), static_cast<std::size_t>(raw.size())), error, lt::from_span);
  test::require(!error, "Cannot parse generated router metadata"); metadata->clear_trackers();
  lt::add_torrent_params options; options.ti = std::move(metadata); options.save_path = path.toStdString();
  options.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
  options.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
  // Intentionally NO upload_mode/stop_when_ready/default_dont_download guard:
  // the discard backend itself must prevent payload filesystem IO.
  auto handle = session.add_torrent(std::move(options), error);
  test::require(!error && handle.is_valid(), "Cannot add generated routed torrent");
  return handle;
}
} // namespace

class MetadataDiskRouterTests final : public QObject {
  Q_OBJECT
private slots:
  void registrationIsBoundedLexicalAndIndependentOfFilesystemExistence() {
    QTemporaryDir area; QVERIFY(area.isValid());
    auto policy = std::make_shared<Policy>();
    const auto root = std::filesystem::path(area.path().toStdString()) / "absent";
    QVERIFY(!std::filesystem::exists(root));
    QVERIFY(policy->registerMetadataPath(root / "child"));
    QVERIFY(policy->isMetadataPath(root / "child"));
    QVERIFY(policy->isMetadataPath(root / "other" / ".." / "child" / ""));
    QVERIFY(!policy->isMetadataPath(root / "child-other"));
    QVERIFY(!policy->isMetadataPath(root));
    QVERIFY(!policy->registerMetadataPath("relative/path"));
    QVERIFY(!policy->registerMetadataPath("/"));
    QVERIFY(!policy->registerMetadataPath(std::string("/bad\0path", 9)));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, policy->isMetadataPath("relative"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, engine::detail::metadataDiskConstructor({}));
    for (std::size_t i = 1; i < Policy::MaximumMetadataPaths; ++i) {
      QVERIFY(policy->registerMetadataPath(root / std::to_string(i)));
    }
    const auto full = policy->registerMetadataPath(root / "one-too-many");
    QVERIFY(!full); QCOMPARE(full.error().code, storage::ErrorCode::Unsupported);
    QVERIFY(policy->registerMetadataPath(root / "child" / "")); // Duplicate normalized registration consumes no slot.
    QVERIFY(!std::filesystem::exists(root));
  }

  void realBackendOwnershipRemovalsAndGlobalIndicesNeverAlias() {
    QTemporaryDir area; QVERIFY(area.isValid());
    auto policy = std::make_shared<Policy>(); DirectDisk engine(policy); Geometry geometry;
    const auto path = (std::filesystem::path(area.path().toStdString()) / "uncreated").string();
    auto firstOwner = std::make_shared<int>(1); std::weak_ptr<int> firstWeak = firstOwner;
    auto secondOwner = std::make_shared<int>(2); std::weak_ptr<int> secondWeak = secondOwner;
    auto first = engine.disk->new_torrent(geometry.parameters(path), firstOwner);
    auto second = engine.disk->new_torrent(geometry.parameters(path), secondOwner);
    QCOMPARE(index(first), std::uint32_t{0}); QCOMPARE(index(second), std::uint32_t{1});
    QCOMPARE(firstOwner.use_count(), 2L); QCOMPARE(secondOwner.use_count(), 2L); // Stock mmap's one owner, no router copy.
    firstOwner.reset(); secondOwner.reset(); QVERIFY(!firstWeak.expired() && !secondWeak.expired());
    const auto oldIndex = static_cast<lt::storage_index_t>(first);
    first.reset(); QVERIFY(firstWeak.expired()); QVERIFY(!secondWeak.expired());
    auto thirdOwner = std::make_shared<int>(3); std::weak_ptr<int> thirdWeak = thirdOwner;
    auto third = engine.disk->new_torrent(geometry.parameters(path), thirdOwner); thirdOwner.reset();
    QCOMPARE(index(third), std::uint32_t{2});
    // Stock mmap can reuse its freed local index0. A stale GLOBAL0 remove
    // cannot remove the new GLOBAL2 storage, nor dispatch stock remove twice.
    engine.disk->remove_torrent(oldIndex); engine.disk->remove_torrent(oldIndex);
    QVERIFY(!thirdWeak.expired()); QVERIFY(!secondWeak.expired());
    QCOMPARE(engine.disk->get_status(third).size(), std::size_t{0});
    third.reset(); QVERIFY(thirdWeak.expired());
    second.reset(); QVERIFY(secondWeak.expired());
    QVERIFY(!std::filesystem::exists(path)); // Constructing stock storage alone stays lazy.
  }

  void disabledForwardsEveryStorageOperationWithoutCreatingOrMovingFiles() {
    QTemporaryDir area; QVERIFY(area.isValid());
    auto policy = std::make_shared<Policy>();
    const auto reserved = (std::filesystem::path(area.path().toStdString()) / "never-created").string();
    const auto moved = (std::filesystem::path(area.path().toStdString()) / "never-moved").string();
    QVERIFY(policy->registerMetadataPath(reserved));
    DirectDisk engine(policy); Geometry geometry;
    auto owner = std::make_shared<int>(7); std::weak_ptr<int> weak = owner;
    auto holder = engine.disk->new_torrent(geometry.parameters(reserved + '/'), owner);
    owner.reset(); QVERIFY(weak.expired()); // Stock disabled backend retains no owner reference.
    struct Results { int calls = 0; bool failed = false, zeroRead = false; };
    auto result = std::make_shared<Results>();
    const auto checked = [result](const lt::storage_error& error) { ++result->calls; result->failed |= bool(error.ec); };
    const lt::peer_request request{lt::piece_index_t{0}, 0, 16384};
    std::array<char, 16384> data{}; data.fill('X');
    QVERIFY(!engine.disk->async_write(holder, request, data.data(), {}, checked));
    engine.disk->async_read(holder, request, [result](lt::disk_buffer_holder buffer, const lt::storage_error& error) {
      ++result->calls; result->failed |= bool(error.ec);
      result->zeroRead = buffer && std::all_of(buffer.data(), buffer.data() + 16384, [](char c) { return c == 0; });
    });
    engine.disk->async_hash(holder, lt::piece_index_t{0}, {}, lt::disk_interface::v1_hash,
        [checked](lt::piece_index_t, const lt::sha1_hash&, const lt::storage_error& error) { checked(error); });
    engine.disk->async_hash2(holder, lt::piece_index_t{0}, 0, {},
        [checked](lt::piece_index_t, const lt::sha256_hash&, const lt::storage_error& error) { checked(error); });
    engine.disk->async_move_storage(holder, moved, {}, [result, moved](lt::status_t value, const std::string& path, const lt::storage_error& error) {
      ++result->calls; result->failed |= bool(error.ec) || value != lt::status_t::no_error || path != moved;
    });
    engine.disk->async_release_files(holder, [result] { ++result->calls; });
    lt::add_torrent_params resume;
    engine.disk->async_check_files(holder, &resume, {}, [result](lt::status_t value, const lt::storage_error& error) {
      ++result->calls; result->failed |= bool(error.ec) || value != lt::status_t::no_error;
    });
    engine.disk->async_stop_torrent(holder, [result] { ++result->calls; });
    // These callbacks are optional in disk_interface, including metadata routes.
    engine.disk->async_release_files(holder, {});
    engine.disk->async_stop_torrent(holder, {});
    engine.disk->async_rename_file(holder, lt::file_index_t{0}, "renamed.bin",
        [result](const std::string& path, lt::file_index_t file, const lt::storage_error& error) {
      ++result->calls; result->failed |= bool(error.ec) || path != "renamed.bin" || file != lt::file_index_t{0};
    });
    engine.disk->async_delete_files(holder, {}, checked);
    engine.disk->async_set_file_priority(holder, geometry.priorities,
        [result](const lt::storage_error& error, lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities) {
      ++result->calls; result->failed |= bool(error.ec) || priorities.size() != 1 || priorities[lt::file_index_t{0}] != lt::download_priority_t{4};
    });
    engine.disk->async_clear_piece(holder, lt::piece_index_t{0}, [result](lt::piece_index_t piece) {
      ++result->calls; result->failed |= piece != lt::piece_index_t{0};
    });
    engine.disk->settings_updated(); engine.disk->submit_jobs();
    QTRY_VERIFY_WITH_TIMEOUT((engine.pump(), result->calls == 12), 5000);
    QVERIFY(!result->failed); QVERIFY(result->zeroRead);
    QCOMPARE(engine.disk->get_status(holder).size(), std::size_t{0});
    engine.counters.set_value(lt::counters::disk_blocks_in_use, 19);
    engine.disk->update_stats_counters(engine.counters);
    QCOMPARE(engine.counters[lt::counters::disk_blocks_in_use], std::int64_t{0}); // Not disabled's synthetic1.
    holder.reset();
    QVERIFY(!std::filesystem::exists(reserved)); QVERIFY(!std::filesystem::exists(moved));
  }

  void lifetimeAdmissionBoundDoesNotRecycleRetiredDisabledIndices() {
    QTemporaryDir area; QVERIFY(area.isValid());
    const auto path = (std::filesystem::path(area.path().toStdString()) / "virtual").string();
    auto policy = std::make_shared<Policy>(); QVERIFY(policy->registerMetadataPath(path));
    DirectDisk engine(policy); Geometry geometry;
    for (std::size_t i = 0; i < Policy::MaximumStorageAdmissions; ++i) {
      auto holder = engine.disk->new_torrent(geometry.parameters(path), {});
      QCOMPARE(index(holder), static_cast<std::uint32_t>(i));
      holder.reset();
    }
    QVERIFY_THROWS_EXCEPTION(std::length_error, engine.disk->new_torrent(geometry.parameters(path), {}));
    QVERIFY(!std::filesystem::exists(path));
  }

  void oneActualSessionWritesOrdinaryPayloadAndDiscardsRegisteredPayload_data() {
    QTest::addColumn<bool>("trailingSeparator");
    QTest::newRow("exact") << false;
    QTest::newRow("equivalent-trailing-separator") << true;
  }
  void oneActualSessionWritesOrdinaryPayloadAndDiscardsRegisteredPayload() {
    QFETCH(bool, trailingSeparator);
    test::OwnedEngineFixture ordinary(false), metadata(true);
    QTRY_VERIFY_WITH_TIMEOUT(ordinary.ready() && metadata.ready(), 30000);
    const auto virtualPath = metadata.root.path() + QStringLiteral("/never-create-metadata-payload");
    QVERIFY(!QFileInfo::exists(virtualPath));
    auto policy = std::make_shared<Policy>();
    QVERIFY(policy->registerMetadataPath(std::filesystem::path(virtualPath.toStdString())));
    const auto sentinelPath = metadata.root.path() + QStringLiteral("/sentinel");
    const QByteArray sentinel("owned-unchanged"); test::write(sentinelPath, sentinel);
    {
      lt::session_params parameters(test::localSettings());
      parameters.disk_io_constructor = engine::detail::metadataDiskConstructor(policy);
      lt::session session(std::move(parameters));
      auto real = add(session, ordinary, ordinary.payload);
      auto discarded = add(session, metadata, virtualPath + (trailingSeparator ? "/" : ""));
      real.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(), ordinary.port()));
      discarded.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(), metadata.port()));
      QTRY_VERIFY_WITH_TIMEOUT(real.status().is_seeding && discarded.status().total_payload_download > 0, 30000);
      for (const auto& file : ordinary.files) {
        QCOMPARE(test::read(ordinary.payload + '/' + file.relative), file.expected);
      }
      QVERIFY(!QFileInfo::exists(virtualPath));
      QCOMPARE(test::read(sentinelPath), sentinel);
      QVERIFY(!discarded.status().is_seeding); // Discarded payload is not durable/verified data.
      session.remove_torrent(discarded); session.remove_torrent(real);
    } // BOTH backends abort/join with this same real LT session.
    QVERIFY(!QFileInfo::exists(virtualPath)); QCOMPARE(test::read(sentinelPath), sentinel);
    QVERIFY(ordinary.intact());
  }
};
QTEST_GUILESS_MAIN(MetadataDiskRouterTests)
#include "metadata_disk_router_tests.moc"
