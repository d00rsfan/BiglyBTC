// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_content_routes.hpp"
#include "media_content_server.hpp"
#include "media_renderer_transport.hpp"
#include "media_renderer_discovery.hpp"
#include "media_http_transfer.hpp"
#include "torrent_http_body.hpp"
#include "torrent_transcode.hpp"
#include "owned_engine_fixture.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QThread>
#include <QUrl>
#include <set>
#include <QXmlStreamReader>
#include <array>
#include <iostream>

using namespace biglybtc;
using namespace std::chrono_literals;
using test::require;

namespace {
constexpr engine::AccountingIdentity identity{601, 602, 603};

void waitFor(const auto& condition, std::chrono::milliseconds timeout = 5s) {
  QElapsedTimer timer;
  timer.start();
  while (!condition()) {
    require(timer.elapsed() < timeout.count(), "Timed out waiting for the media job or session");
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
}

bool terminal(media::FfmpegJobState state) {
  using State = media::FfmpegJobState;
  return state == State::Produced || state == State::Cancelled || state == State::TimedOut || state == State::Failed;
}

int fixtureChild(const QString& outputPath) {
  std::array<char, 4096> buffer;
  QCryptographicHash digest(QCryptographicHash::Sha256);
  while (std::cin.read(buffer.data(), buffer.size()) || std::cin.gcount() > 0) {
    digest.addData(QByteArrayView(buffer.data(), std::cin.gcount()));
  }
  test::write(outputPath, digest.result());
  std::cout << "frame=1\ntotal_size=32\nprogress=end\n";
  return 0;
}

media::FfmpegProgram program(const QString& path) {
  QFile executable(path);
  require(executable.open(QIODevice::ReadOnly), "Media tool is unavailable");
  QCryptographicHash hash(QCryptographicHash::Sha256);
  require(hash.addData(&executable), "Cannot hash the media tool");
  return {path, hash.result()};
}

struct Fixture {
  QTemporaryDir root;
  QString payload = root.filePath("payload/sample.mkv");
  std::optional<storage::ProfileLease> lease;
  std::shared_ptr<engine::PersistentSession> session;
  std::optional<media::TorrentTranscode::Input> input;

  explicit Fixture(const QByteArray& content, bool leaveLastPieceMissing = false) {
    require(root.isValid() && !content.isEmpty(), "Own generated torrent data");
    constexpr qsizetype pieceSize = 16384;
    libtorrent::entry info(libtorrent::entry::dictionary_type{});
    info["name"] = "sample.mkv";
    info["piece length"] = pieceSize;
    info["length"] = content.size();
    info["private"] = 1;
    std::string parts;
    for (qsizetype offset = 0; offset < content.size(); offset += pieceSize) {
      parts += QCryptographicHash::hash(QByteArrayView(content).sliced(offset,
          std::min(pieceSize, content.size() - offset)), QCryptographicHash::Sha1).toStdString();
    }
    info["pieces"] = parts;
    const auto rawHash = QCryptographicHash::hash(QByteArray::fromStdString(test::encoded(info)),
        QCryptographicHash::Sha1);
    const auto hash = core::TorrentHash::parse(rawHash.toStdString());
    require(hash.has_value(), "Read generated torrent identity");

    const auto profile = root.filePath("profile");
    const auto state = profile + "/active/" + QString::fromStdString(hash->hex()) + ".dat";
    libtorrent::entry torrent(libtorrent::entry::dictionary_type{});
    torrent["info"] = std::move(info);
    torrent["attributes"]["storetypes"] = libtorrent::entry::list_type{libtorrent::entry("L")};
    torrent["attributes"]["networks"] = libtorrent::entry::list_type{libtorrent::entry("Public")};
    test::write(state, QByteArray::fromStdString(test::encoded(torrent)));
    libtorrent::entry row(libtorrent::entry::dictionary_type{});
    row["torrent_hash"] = rawHash.toStdString();
    row["torrent"] = state.toStdString();
    row["save_dir"] = QFileInfo(payload).absolutePath().toStdString();
    row["save_file"] = "sample.mkv";
    row["persistent"] = 1;
    row["state"] = 75;
    row["position"] = 1;
    row["downloaded"] = 0;
    row["uploaded"] = 0;
    libtorrent::entry inventory(libtorrent::entry::dictionary_type{});
    inventory["downloads"] = libtorrent::entry::list_type{row};
    test::write(profile + "/downloads.config", QByteArray::fromStdString(test::encoded(inventory)));
    test::write(profile + "/tables.config", "de");
    auto physical = content;
    if (leaveLastPieceMissing) {
      physical[physical.size() - 1] ^= 1;
    }
    test::write(payload, physical);

    auto held = storage::ProfileLease::acquire(profile.toStdString());
    require(held.has_value(), "Hold generated torrent profile");
    lease.emplace(std::move(*held));
    auto created = engine::PersistentSession::create(identity);
    require(created.has_value(), "Create generated torrent session");
    session = std::move(*created);
    engine::NetworkPolicy policy;
    policy.mode = engine::NetworkMode::IsolatedLoopback;
    require(bool(session->configureNetwork(policy)) && bool(session->startIdle(*lease)), "Start owned loopback session");
    auto started = session->startManaged(*lease, {*hash, 0, true, false});
    require(bool(started), started ? "" : started.error().message.c_str());
    waitFor([&] {
      require(bool(session->poll()), "Poll generated torrent owner");
      for (const auto& status : session->managedDownloads()) {
        if (status.hash == *hash && status.checked && status.retained && !status.failure) {
          input = media::TorrentTranscode::Input{identity, *hash, status.admission, 0};
          return true;
        }
      }
      return false;
    });
  }

  void close() {
    const auto result = session->close(std::chrono::steady_clock::now() + 5s);
    require(result.sessionJoined && result.retirement.storageReleased && !result.checkpointFailure,
        "Join generated torrent session");
    session.reset();
  }
};

void fixtureCases() {
  using State = media::FfmpegJobState;
  QByteArray content(12 * 16384 + 13, '\0');
  for (qsizetype offset = 0; offset < content.size(); ++offset) {
    content[offset] = static_cast<char>((offset * 13 + offset / 16384 * 11) % 251);
  }
  media::FfmpegJob job;
  job.program = program(QCoreApplication::applicationFilePath());
  job.deadline = 4s;
  {
    Fixture fixture(content);
    auto stale = *fixture.input;
    ++stale.engine.engineEpoch;
    media::TorrentTranscode foreign(fixture.session, stale);
    require(!foreign.start(job) && foreign.state() == State::Idle, "Reject a foreign session generation");

    media::TorrentTranscode transfer(fixture.session, *fixture.input);
    auto rejected = job;
    rejected.program.sha256[0] ^= 1;
    require(!transfer.start(rejected), "Reject an unpinned helper");
    waitFor([&] { return bool(transfer.start(job)); });
    waitFor([&] { return terminal(transfer.state()); });
    require(transfer.state() == State::Produced, transfer.diagnostics().toStdString().c_str());
    const auto result = transfer.resultFile();
    require(test::read(result) == QCryptographicHash::hash(content, QCryptographicHash::Sha256),
        "The joined child read precisely the verified pieces through EOF");
    require(bool(fixture.session->stopManaged(fixture.input->hash)), "Retire the source admission");
    require(transfer.resultFile().isEmpty() && transfer.state() == State::Failed, "Withdraw the retired result");
    fixture.close();
  }
  {
    Fixture fixture(content, true);
    media::TorrentTranscode stalled(fixture.session, *fixture.input);
    auto timeout = job;
    timeout.deadline = 1300ms;
    require(bool(stalled.start(timeout)), "Start with an incomplete generated torrent");
    waitFor([&] { return terminal(stalled.state()); });
    require(stalled.state() == State::TimedOut && stalled.resultFile().isEmpty(),
        "An unverified final piece must block output publication");

    media::TorrentTranscode cancelled(fixture.session, *fixture.input);
    waitFor([&] { return bool(cancelled.start(job)); });
    waitFor([&] { return cancelled.state() == State::Running; });
    require(cancelled.cancel(), "Cancel a running media job");
    waitFor([&] { return terminal(cancelled.state()); });
    require(cancelled.state() == State::Cancelled && cancelled.resultFile().isEmpty(), "Join the cancelled helper");

    media::TorrentTranscode retired(fixture.session, *fixture.input);
    waitFor([&] { return bool(retired.start(job)); });
    fixture.close();
    waitFor([&] { return terminal(retired.state()); });
    require(retired.state() == State::Failed && retired.resultFile().isEmpty(), "Join after owner shutdown");
  }
}
void rangeCases() {
  using media::MediaHttpMethod;
  using media::MediaHttpRange;
  using media::TorrentHttpBody;
  using Code = storage::ErrorCode;
  QByteArray content(4 * 16384 + 13, '\0');
  for (qsizetype offset = 0; offset < content.size(); ++offset) {
    content[offset] = static_cast<char>((offset * 13 + offset / 16384 * 11) % 251);
  }
  {
    Fixture fixture(content);
    auto target = *fixture.input;
    target.file = 1;
    require(!TorrentHttpBody::open(fixture.session, target, MediaHttpMethod::Get, std::nullopt),
        "Reject an unbound file index");
    auto head = TorrentHttpBody::open(fixture.session, *fixture.input, MediaHttpMethod::Head, "bytes=2-3");
    require(head && (*head)->response().length == static_cast<std::uint64_t>(content.size()) &&
        !(*head)->response().sendBody, "HEAD reports an authoritative size");
    auto headRead = (*head)->read(64);
    require(headRead && *headRead && (*headRead)->empty(), "HEAD submits no body bytes");
    auto outside = TorrentHttpBody::open(fixture.session, *fixture.input, MediaHttpMethod::Get, "bytes=100000-");
    require(outside && (*outside)->response().status == MediaHttpRange::Status::Unsatisfiable &&
        (*outside)->response().contentRange() == "bytes */65549", "416 reports the authoritative size");

    auto range = TorrentHttpBody::open(fixture.session, *fixture.input, MediaHttpMethod::Get, "bytes=16381-49164");
    require(range && (*range)->response().contentRange() == "bytes 16381-49164/65549", "Select a cross-piece HTTP span");
    const auto invalid = (*range)->read(65537);
    require(!invalid && invalid.error().code == Code::InvalidFormat, "Reject excess transport capacity");
    QByteArray delivered;
    waitFor([&] {
      auto chunk = (*range)->read(8192);
      require(bool(chunk), chunk ? "" : chunk.error().message.c_str());
      if (!*chunk) {
        return false;
      }
      if ((*chunk)->empty()) {
        return true;
      }
      require((*chunk)->size() <= 8192, "Respect transport size");
      delivered.append((*chunk)->data(), static_cast<qsizetype>((*chunk)->size()));
      return false;
    });
    require(delivered == content.mid(16381, 49164 - 16381 + 1), "Read exactly the selected verified byte span");
    require(bool(fixture.session->stopManaged(fixture.input->hash)), "Retire the media source");
    const auto retired = (*range)->read(8192);
    require(!retired && retired.error().code == Code::Conflict, "Retired media cannot supply bytes");
    fixture.close();
  }
  {
    Fixture fixture(content, true);
    auto tail = TorrentHttpBody::open(fixture.session, *fixture.input, MediaHttpMethod::Get, "bytes=-13");
    require(tail && (*tail)->response().length == 13, "Select the corrupt final piece");
    QElapsedTimer observation;
    observation.start();
    waitFor([&] {
      const auto blocked = (*tail)->read(64);
      require(blocked && !*blocked, "An unverified response tail must remain pending");
      return observation.elapsed() >= 500;
    });
    (*tail)->cancel();
    const auto cancelled = (*tail)->read(64);
    require(!cancelled && cancelled.error().code == Code::Cancelled, "A disconnect retires a pending media response");
    auto head = TorrentHttpBody::open(fixture.session, *fixture.input, MediaHttpMethod::Get, "bytes=0-1");
    require(bool(head), "Admit another response on the current owner");
    waitFor([&] {
      const auto first = (*head)->read(64);
      require(bool(first), first ? "" : first.error().message.c_str());
      return *first && (*first)->size() == 2 && std::equal((*first)->begin(), (*first)->end(), content.constData());
    });
    fixture.close();
  }
  std::cout << "PASS authoritative HTTP ranges, exact verified body, disconnect and retirement\n";
}
struct SocketPair {
  QTcpServer listener;
  QTcpSocket client;
  QTcpSocket* sender = nullptr;

  SocketPair() {
    listener.setProxy(QNetworkProxy::NoProxy);
    client.setProxy(QNetworkProxy::NoProxy);
    require(listener.listen(QHostAddress::LocalHost), "Listen on the isolated loopback interface");
    client.connectToHost(QHostAddress::LocalHost, listener.serverPort());
    waitFor([&] { return listener.hasPendingConnections() && client.state() == QAbstractSocket::ConnectedState; });
    sender = listener.nextPendingConnection();
    listener.close();
    require(sender && sender->state() == QAbstractSocket::ConnectedState, "Receive the isolated loopback connection");
  }
};

void wireCases() {
  using media::MediaHttpMethod;
  using media::MediaHttpTransfer;
  using media::TorrentHttpBody;
  using State = MediaHttpTransfer::State;
  QByteArray content(4 * 16384 + 13, '\0');
  for (qsizetype offset = 0; offset < content.size(); ++offset) {
    content[offset] = static_cast<char>((offset * 53 + offset / 16384 * 13) % 251);
  }
  Fixture fixture(content, true);
  const auto response = [&](MediaHttpMethod method, std::string_view range) {
    SocketPair connection;
    media::MediaHttpTransfer transfer;
    auto body = TorrentHttpBody::open(fixture.session, *fixture.input, method, range);
    require(body && transfer.start(*connection.sender, std::move(*body), 2s), "Start an owned HTTP media response");
    QByteArray wire;
    waitFor([&] {
      wire += connection.client.readAll();
      return connection.client.state() == QAbstractSocket::UnconnectedState;
    });
    require(transfer.state() == State::Sent, "Drain the HTTP response before closing");
    return wire;
  };
  const auto partial = response(MediaHttpMethod::Get, "bytes=16380-32999");
  const auto headEnd = partial.indexOf("\r\n\r\n");
  require(headEnd > 0 && partial.startsWith("HTTP/1.1 206 ") &&
      partial.contains("Content-Range: bytes 16380-32999/65549\r\n") &&
      partial.contains("Content-Length: 16620\r\n") &&
      partial.mid(headEnd + 4) == content.mid(16380, 16620), "HTTP 206 carries precisely the verified bytes");
  const auto head = response(MediaHttpMethod::Head, "bytes=0-1");
  require(head.startsWith("HTTP/1.1 200 ") && head.endsWith("\r\n\r\n") &&
      head.contains("Content-Length: 65549\r\n") && !head.contains("Content-Range:"),
      "HEAD carries a whole-resource length with no body");
  const auto unsatisfiable = response(MediaHttpMethod::Get, "bytes=70000-");
  require(unsatisfiable.startsWith("HTTP/1.1 416 ") && unsatisfiable.endsWith("\r\n\r\n") &&
      unsatisfiable.contains("Content-Length: 0\r\n") && unsatisfiable.contains("Content-Range: bytes */65549\r\n"),
      "HTTP 416 carries the authoritative representation size with no body");

  {
    SocketPair connection;
    MediaHttpTransfer transfer;
    auto body = TorrentHttpBody::open(fixture.session, *fixture.input, MediaHttpMethod::Get, "bytes=-13");
    require(body && transfer.start(*connection.sender, std::move(*body), 400ms), "Start a blocked media response");
    waitFor([&] { return connection.client.state() == QAbstractSocket::UnconnectedState; });
    require(transfer.state() == State::TimedOut && connection.client.bytesAvailable() == 0,
        "An unavailable first piece times out before the server emits headers or data");
  }
  {
    SocketPair connection;
    MediaHttpTransfer transfer;
    auto body = TorrentHttpBody::open(fixture.session, *fixture.input, MediaHttpMethod::Get, "bytes=65528-");
    require(body && transfer.start(*connection.sender, std::move(*body), 2s), "Start a midstream disconnect control");
    QByteArray prefix;
    waitFor([&] {
      prefix += connection.client.readAll();
      const auto header = prefix.indexOf("\r\n\r\n");
      return header >= 0 && prefix.size() >= header + 4 + 8;
    });
    require(prefix.endsWith(content.mid(65528, 8)) && prefix.contains("Content-Length: 21\r\n"),
        "Send only the verified prefix before the missing final piece");
    connection.client.abort();
    waitFor([&] { return transfer.state() != State::Sending; });
    require(transfer.state() == State::Disconnected, "Client disconnect releases the pending request");
  }
  require(response(MediaHttpMethod::Get, "bytes=0-1").endsWith(content.first(2)),
      "Another response can acquire the released read slot");
  fixture.close();
  {
    Fixture large(QByteArray(512 * 1024, 'm'));
    SocketPair connection;
    connection.client.setReadBufferSize(64);
    connection.client.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4096);
    connection.sender->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4096);
    MediaHttpTransfer transfer;
    auto body = TorrentHttpBody::open(large.session, *large.input, MediaHttpMethod::Get, std::nullopt);
    require(body && transfer.start(*connection.sender, std::move(*body), 1s), "Start the slow-client control");
    bool sawBackpressure = false;
    waitFor([&] {
      const auto queued = connection.sender->bytesToWrite();
      require(queued <= 64 * 1024 + 512, "The slow client must not accumulate an unbounded Qt write queue");
      sawBackpressure |= queued > 0;
      return transfer.state() != State::Sending;
    });
    require(sawBackpressure && transfer.state() == State::TimedOut, "A slow client times out after write progress stops");
    large.close();
  }
  std::cout << "PASS loopback HTTP 206, HEAD, 416, verified prefix, timeouts, backpressure and disconnect\n";
}
void routeCases() {
  using media::MediaContentRoutes;
  using media::MediaRequestError;
  using media::MediaHttpRange;
  Fixture fixture(QByteArray(16397, 'v'));
  const auto peer = QHostAddress::LocalHost;
  const auto hash = QByteArray::fromStdString(fixture.input->hash.hex());
  const QByteArray path = "/Content/" + hash.toLower() + "-0.mp4?sid=17";
  const auto request = [&](QByteArrayView target, QByteArrayView headers = {},
      QByteArrayView method = "GET", QByteArrayView authority = "localhost:48000") {
    QByteArray raw;
    raw += method;
    raw += ' ';
    raw += target;
    raw += " HTTP/1.1\r\nHost: ";
    raw += authority;
    raw += "\r\n";
    raw += headers;
    raw += "\r\n";
    return raw;
  };
  const auto raw = request(path, "rAnGe: bytes=16380-16395\r\n");
  const auto view = [](const QByteArray& value) {
    return std::string_view(value.constData(), static_cast<std::size_t>(value.size()));
  };
  MediaContentRoutes routes(fixture.session, media::MediaBasicCredentials{
      "viewer", QCryptographicHash::hash("generated", QCryptographicHash::Sha1)});
  const QByteArray auth = "Authorization: Basic " + QByteArray("viewer:generated").toBase64() + "\r\n";
  const auto accepted = request(path, auth + "Range: bytes=16380-16395\r\n");
  require(routes.resolve(peer, view(raw)) == std::unexpected(MediaRequestError::Unauthorized),
      "Check credentials before revealing resource membership");
  require(routes.resolve(peer, view(accepted)) == std::unexpected(MediaRequestError::NotFound),
      "A valid URL cannot open an unpublished resource");
  auto stale = *fixture.input;
  ++stale.admission;
  require(!routes.publish(stale) && routes.publish(*fixture.input) && !routes.withdraw(stale),
      "Bind publication and withdrawal to the actual admission");
  require(routes.resolve(QHostAddress("192.0.2.12"), view(accepted)) == std::unexpected(MediaRequestError::Forbidden),
      "Reject a non-loopback peer");
  {
    auto response = routes.resolve(peer, view(accepted));
    require(response && (*response)->response().status == MediaHttpRange::Status::Partial &&
        (*response)->response().contentRange() == "bytes 16380-16395/16397", "Route the original path to the held file");
    std::size_t bytes = 0;
    waitFor([&] {
      auto result = (*response)->read(8192);
      require(bool(result), "Read the route's verified payload");
      if (!*result) {
        return false;
      }
      bytes += (*result)->size();
      return (*result)->empty();
    });
    require(bytes == 16, "Route the exact requested span across torrent pieces");
  }
  require(routes.selectRendererType(peer, view(path), u"video/x-mkv"),
      "Bind the actual renderer's selected original MIME alternative");
  auto preferred = routes.resolve(peer, view(accepted));
  auto other = routes.resolve(QHostAddress("127.0.0.2"), view(accepted));
  require(preferred && (*preferred)->contentType() == "video/x-mkv" &&
      other && (*other)->contentType() == "video/x-matroska",
      "Serve the alternative to the selected peer and preserve the primary type for other peers");
  require(!routes.selectRendererType(peer, view(path), u"application/fictional"),
      "Refuse a type absent from the published resource");
  routes.clearRendererType();
  auto cleared = routes.resolve(peer, view(accepted));
  require(cleared && (*cleared)->contentType() == "video/x-matroska",
      "Restore the original default after clearing the exact renderer selection");
  std::cout << "PASS scope an original MIME alternative to the selected peer and publication\n";

  const auto check = [&](QByteArray rawRequest, MediaRequestError expected) {
    require(routes.resolve(peer, view(rawRequest)) == std::unexpected(expected),
        "Malformed, unowned or unauthorized request passed");
  };
  auto head = routes.resolve(QHostAddress::LocalHostIPv6,
      view(request("/Content/" + hash + "-%30.Video%20File", auth, "HEAD", "[::1]:48000")));
  require(head && !(*head)->response().sendBody, "Decode the original resource index and ignore the display suffix");
  check(request(path, "Authorization: Basic " + QByteArray("viewer:wrong").toBase64() + "\r\n"),
      MediaRequestError::Unauthorized);
  check(request(path, "Authorization: Bearer " + QByteArray("viewer:generated").toBase64() + "\r\n"),
      MediaRequestError::Unauthorized);
  check(request(path, auth + auth), MediaRequestError::BadRequest);
  check(request(path, auth + "Range: bytes=0-1\r\nrange: bytes=3-4\r\n"), MediaRequestError::BadRequest);
  check(request(path, auth + "Content-Length: 1\r\n"), MediaRequestError::BadRequest);
  check(request(path, auth + "Transfer-Encoding: chunked\r\n"), MediaRequestError::BadRequest);
  check(request(path, auth, "GET", "example.invalid"), MediaRequestError::BadRequest);
  check(request(path, auth, "GET", "127.0.0.1:65536"), MediaRequestError::BadRequest);
  check(request(path, auth + "If-Range: \"1234\"\r\n"), MediaRequestError::Unsupported);
  check(request(path, auth + "Range: bytes=3-2\r\n"), MediaRequestError::RangeRejected);
  check(request("/Platform?cid=AAAA", auth), MediaRequestError::NotFound);
  check(request("/Content/" + hash + "-1.mp4", auth), MediaRequestError::NotFound);
  check(request("/Content/" + hash + "-0.%2Ffile", auth), MediaRequestError::BadRequest);
  check(request("/Content/" + hash + "-0.%ZZ", auth), MediaRequestError::BadRequest);
  check(request(path, auth, "POST"), MediaRequestError::Unsupported);
  check(request(path, auth) + request(path, auth), MediaRequestError::BadRequest);
  check("abc", MediaRequestError::BadRequest);
  check(request(path, auth + "Unused: " + QByteArray(8192, 'q') + "\r\n"), MediaRequestError::BadRequest);
  MediaContentRoutes local(fixture.session);
  require(local.publish(*fixture.input) && local.resolve(peer, view(raw)), "Explicitly uncredentialed local route");
  auto inFlight = routes.resolve(peer, view(accepted));
  require(bool(inFlight) && routes.publish(*fixture.input) && bool((*inFlight)->read(64)),
      "Republishing the same admission preserves a response");
  require(routes.withdraw(*fixture.input) && !routes.resolve(peer, view(accepted)),
      "Withdraw exactly the registered source");
  const auto revoked = (*inFlight)->read(64);
  require(!revoked && revoked.error().code == storage::ErrorCode::Conflict, "Revoke the active response");
  require(routes.publish(*fixture.input) && bool(fixture.session->stopManaged(fixture.input->hash)),
      "Retire the published torrent");
  check(accepted, MediaRequestError::NotFound);
  fixture.close();
  std::cout << "PASS original Content route, explicit current file, loopback policy and Basic authorization\n";
}
void listenerCases() {
  using media::MediaContentServer;
  Fixture fixture(QByteArray(16397, 't'));
  MediaContentServer listener(fixture.session, media::MediaBasicCredentials{
      "viewer", QCryptographicHash::hash("generated", QCryptographicHash::Sha1)});
  const MediaContentServer::Limits limits{250ms, 2s, 2};
  require(listener.publish(*fixture.input) && listener.start(0, limits) && listener.port() != 0,
      "Bind one explicit file to a loopback listener");
  require(!listener.start(0, limits), "Prevent overlapping listener starts");
  const auto port = listener.port();
  const QByteArray path = "/Content/" + QByteArray::fromStdString(fixture.input->hash.hex()) + "-0.mov";
  const auto auth = QByteArray("viewer:generated").toBase64();
  const QByteArray head = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\n";
  const QByteArray headers = "Authorization: Basic " + auth + "\r\nRange: bytes=16380-16393\r\n\r\n";
  const auto dial = [&](QTcpSocket& client) {
    client.setProxy(QNetworkProxy::NoProxy);
    client.connectToHost(QHostAddress::LocalHost, port);
    waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
  };
  const auto readUntilClosed = [](QTcpSocket& client) {
    QByteArray output;
    waitFor([&] {
      output += client.readAll();
      return client.state() == QAbstractSocket::UnconnectedState;
    });
    return output;
  };
  const auto roundTrip = [&](const QByteArray& request) {
    QTcpSocket client;
    dial(client);
    require(client.write(request) == request.size(), "Queue loopback request");
    return readUntilClosed(client);
  };
  const auto unauthorized = roundTrip(head + "\r\n");
  require(unauthorized.startsWith("HTTP/1.1 401 ") &&
      unauthorized.contains("WWW-Authenticate: Basic realm=\"BiglyBT Media Server\"\r\n"),
      "Challenge unauthenticated media requests");
  {
    QTcpSocket client;
    dial(client);
    const auto first = head + headers.first(12);
    require(client.write(first) == first.size(), "Write the first header fragment");
    waitFor([&] { return client.bytesToWrite() == 0; });
    require(client.bytesAvailable() == 0, "Wait for complete HTTP framing");
    const auto rest = headers.sliced(12);
    require(client.write(rest) == rest.size(), "Write the trailing header fragment");
    const auto response = readUntilClosed(client);
    require(response.startsWith("HTTP/1.1 206 ") && response.endsWith(QByteArray(14, 't')) &&
        response.contains("Content-Type: video/x-matroska\r\n"),
        "An authorized range uses the held file metadata across the piece boundary");
    require(response.contains("contentFeatures.dlna.org: DLNA.ORG_PN=MPEG_PS_NTSC;DLNA.ORG_OP=01;DLNA.ORG_CI=0;"
          "DLNA.ORG_FLAGS=01700000000000000000000000000000\r\n") &&
        response.contains("transferMode.dlna.org: Streaming\r\n"),
        "Advertise the legacy video class and byte seeking for ordinary verified source data");
  }
  const auto attachmentHead = QByteArray(head).replace(path, path + "?action=download");
  const auto attachment = roundTrip(attachmentHead + headers);
  require(attachment.startsWith("HTTP/1.1 206 ") &&
      attachment.contains("Content-Type: application/octet-stream\r\n") &&
      attachment.contains("Content-Disposition: attachment; filename*=UTF-8''sample.mkv\r\n") &&
      !attachment.contains("contentFeatures.dlna.org:") && !attachment.contains("transferMode.dlna.org:"),
      "Attachment headers use the actual file name and keep the requested byte range");
  const auto bad = roundTrip(head + "Authorization: Basic " + auth + "\r\nContent-Length: 10\r\n\r\n");
  require(bad.startsWith("HTTP/1.1 400 "), "Reject framed requests with bodies");
  {
    QTcpSocket client;
    dial(client);
    require(client.write("GET /Con") == 8, "Begin a stalled request");
    require(readUntilClosed(client).isEmpty(), "A partial header times out without a payload");
  }
  require(listener.withdraw(*fixture.input) && roundTrip(head + headers).startsWith("HTTP/1.1 404 "),
      "Withdrawal removes the live URI");
  require(listener.publish(*fixture.input), "Restore an explicit publication");
  {
    QTcpSocket client;
    dial(client);
    listener.stop();
    require(readUntilClosed(client).isEmpty() && listener.port() == 0, "Stop closes the listener and active sockets");
  }
  fixture.close();
  std::cout << "PASS loopback content listener, fragmented request, challenge, range and withdrawal\n";
}
QByteArray transcodedHttp(Fixture& fixture, const media::FfmpegJob& job) {
  using State = media::FfmpegJobState;
  media::MediaContentServer listener(fixture.session, media::MediaBasicCredentials{
      "viewer", QCryptographicHash::hash("generated", QCryptographicHash::Sha1)});
  require(listener.start(0, {1s, 2s, 2}) && listener.publish(*fixture.input), "Start raw and optional converted media routes");
  const auto transcode = std::make_shared<media::TorrentTranscode>(fixture.session, *fixture.input);
  require(bool(transcode->start(job)) && !listener.publishTranscode(*fixture.input, transcode),
      "Keep the unjoined output out of the directory");
  waitFor([&] { return terminal(transcode->state()); }, 10s);
  require(transcode->state() == State::Produced, transcode->diagnostics().toStdString().c_str());
  const auto expected = test::read(transcode->resultFile());
  require(expected.size() > 15, "Obtain a nonempty owned conversion");
  auto stale = *fixture.input;
  ++stale.admission;
  require(!listener.publishTranscode(stale, transcode), "Reject a different source admission");
  const auto renderer = listener.publishTranscode(*fixture.input, transcode);
  require(renderer && renderer->didl.contains(renderer->url) && renderer->didl.contains("size=\"" +
      QByteArray::number(expected.size()) + '"') && renderer->types.contains(QStringLiteral("video/mp4")),
      "Renderer DIDL uses the finished target size, MIME type and URL");
  const auto again = listener.publishTranscode(*fixture.input, transcode);
  require(again && again->url == renderer->url, "Maintain the same link for the same approved result");
  const auto browse = listener.browse({4, media::MediaBrowseMode::DirectChildren, 0, 0});
  require(browse && browse->totalMatches == 2 && browse->didl.contains(renderer->url) &&
      browse->didl.contains(listener.publishedUrl(*fixture.input)), "Advertise the original and transcode independently");

  const auto auth = "Authorization: Basic " + QByteArray("viewer:generated").toBase64() + "\r\n";
  const auto request = [&](const QByteArray& url, QByteArrayView method = "GET", QByteArrayView headers = {}) {
    QTcpSocket client;
    client.setProxy(QNetworkProxy::NoProxy);
    client.connectToHost(QHostAddress::LocalHost, listener.port());
    waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
    const auto path = QUrl::fromEncoded(url).path(QUrl::FullyEncoded).toLatin1();
    QByteArray raw(method.toByteArray() + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1:" +
        QByteArray::number(listener.port()) + "\r\n" + headers.toByteArray() + "\r\n");
    require(client.write(raw) == raw.size(), "Queue an isolated transcode HTTP request");
    QByteArray response;
    waitFor([&] {
      response += client.readAll();
      return client.state() == QAbstractSocket::UnconnectedState;
    });
    return response;
  };
  const auto fileName = transcode->resultFile();
  require(QFile::rename(fileName, fileName + QStringLiteral(".joined")), "Move the generated pathname after publication");
  test::write(fileName, "foreign fixture file at the retired path");
  require(request(renderer->url).startsWith("HTTP/1.1 401 "), "Converted media inherits listener authentication");
  const auto head = request(renderer->url, "HEAD", auth);
  require(head.startsWith("HTTP/1.1 200 ") && head.endsWith("\r\n\r\n") &&
      head.contains("Content-Length: " + QByteArray::number(expected.size()) + "\r\n"), "Report encoded size on HEAD");
  const auto range = request(renderer->url, "GET", auth + "Range: bytes=3-14\r\n");
  const auto header = range.indexOf("\r\n\r\n");
  require(range.startsWith("HTTP/1.1 206 ") && header > 0 &&
      range.contains("Content-Range: bytes 3-14/" + QByteArray::number(expected.size()) + "\r\n") &&
      range.sliced(header + 4) == expected.sliced(3, 12), "Use the held descriptor for exact encoded range bytes");
  require(head.contains("contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_CI=1;") &&
      range.contains("contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_CI=1;") &&
      range.contains("transferMode.dlna.org: Streaming\r\n"),
      "Advertise conversion and byte seeking for a joined encoded artifact");
  const auto beyond = request(renderer->url, "GET", auth + "Range: bytes=" + QByteArray::number(expected.size()) + "-\r\n");
  require(beyond.startsWith("HTTP/1.1 416 ") && !beyond.contains("contentFeatures.dlna.org:"),
      "Reject a byte range past the encoded result");
  const auto full = request(renderer->url, "GET", auth);
  const auto fullHeader = full.indexOf("\r\n\r\n");
  require(full.startsWith("HTTP/1.1 200 ") && fullHeader > 0 && full.contains("Content-Type: video/mp4\r\n") &&
      full.sliced(fullHeader + 4) == expected, "Serve the exact produced bytes with the converted MIME type");
  require(listener.withdrawTranscode(transcode) && request(renderer->url, "GET", auth).startsWith("HTTP/1.1 404 ") &&
      request(listener.publishedUrl(*fixture.input), "HEAD", auth).startsWith("HTTP/1.1 200 "),
      "Withdraw only the converted URL");
  const auto fresh = listener.publishTranscode(*fixture.input, transcode);
  require(fresh && fresh->url != renderer->url, "Generate a new URL for the next publication");
  require(bool(fixture.session->stopManaged(fixture.input->hash)) && request(fresh->url, "GET", auth).startsWith("HTTP/1.1 404 "),
      "Retire a second publication with its original session admission");
  listener.stop();
  std::cout << "PASS owned HTTP delivery of the joined transcode, pinned inode and source retirement\n";
  return full.sliced(fullHeader + 4);
}

void progressiveHttp(const QByteArray& input, media::FfmpegJob job) {
  using State = media::FfmpegJobState;
  Fixture fixture(input, true);
  media::MediaContentServer listener(fixture.session, media::MediaBasicCredentials{
      "viewer", QCryptographicHash::hash("generated", QCryptographicHash::Sha1)});
  require(listener.start(0, {1s, 5s, 4}), "Own a loopback authenticated progressive media server");
  const auto transcode = std::make_shared<media::TorrentTranscode>(fixture.session, *fixture.input);
  job.fragmentMp4 = true;
  job.deadline = 12s;
  require(bool(transcode->start(job)), "Start fragmented MP4 against verified torrent pieces");
  std::optional<media::MediaRendererResource> resource;
  waitFor([&] {
    resource = listener.publishTranscode(*fixture.input, transcode);
    return resource.has_value();
  }, 10s);
  require(transcode->state() == State::Running && transcode->resultFile().isEmpty() &&
      resource->didl.contains(resource->url) && !resource->didl.contains("size="),
      "Advertise an ongoing conversion without a guessed media size");
  const auto path = QUrl::fromEncoded(resource->url).path(QUrl::FullyEncoded).toLatin1();
  const auto auth = "Authorization: Basic " + QByteArray("viewer:generated").toBase64() + "\r\n";
  const auto request = [&](QTcpSocket& client, QByteArrayView method, QByteArrayView fields, QByteArrayView version = "1.1") {
    client.setProxy(QNetworkProxy::NoProxy);
    client.connectToHost(QHostAddress::LocalHost, listener.port());
    waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
    const QByteArray raw = method.toByteArray() + ' ' + path + " HTTP/" + version.toByteArray() +
        "\r\nHost: 127.0.0.1:" + QByteArray::number(listener.port()) + "\r\n" + fields.toByteArray() + "\r\n";
    require(client.write(raw) == raw.size(), "Write a progressive media HTTP request");
  };
  const auto collect = [](QTcpSocket& client) {
    QByteArray response;
    waitFor([&] {
      response += client.readAll();
      return client.state() == QAbstractSocket::UnconnectedState;
    });
    return response;
  };
  const auto whole = [&](QByteArrayView method, QByteArrayView fields, QByteArrayView version = "1.1") {
    QTcpSocket client;
    request(client, method, fields, version);
    return collect(client);
  };
  require(whole("GET", {}).startsWith("HTTP/1.1 401 "), "Protect the live resource with the listener credentials");
  require(whole("GET", auth + "Range: bytes=0-3\r\n").startsWith("HTTP/1.1 400 "),
      "Refuse a fixed range on unfinished media");
  require(whole("GET", auth, "1.0").startsWith("HTTP/1.1 400 "),
      "Require HTTP/1.1 for live chunk framing");
  const auto head = whole("HEAD", auth);
  require(head.startsWith("HTTP/1.1 200 ") && head.contains("Transfer-Encoding: chunked\r\n") &&
      head.contains("Accept-Ranges: none\r\n") && !head.contains("Content-Length: ") &&
      head.contains("contentFeatures.dlna.org: DLNA.ORG_OP=00;DLNA.ORG_CI=1;") &&
      head.contains("transferMode.dlna.org: Streaming\r\n") &&
      head.indexOf("\r\n\r\n") == head.size() - 4, "Return live HEAD headers without any chunk body or seek claim");

  QTcpSocket client;
  request(client, "GET", auth);
  QByteArray wire;
  QByteArray first;
  waitFor([&] {
    wire += client.readAll();
    const auto header = wire.indexOf("\r\n\r\n");
    if (header < 0) { return false; }
    const auto countEnd = wire.indexOf("\r\n", header + 4);
    if (countEnd < 0) { return false; }
    bool valid;
    const auto count = wire.mid(header + 4, countEnd - header - 4).toLongLong(&valid, 16);
    if (!valid || count < 8 || wire.size() < countEnd + 4 + count) { return false; }
    first = wire.mid(countEnd + 2, count);
    return true;
  }, 6s);
  require(first.mid(4, 4) == "ftyp" && wire.startsWith("HTTP/1.1 200 ") &&
      wire.contains("Content-Type: video/mp4\r\n") && wire.contains("Transfer-Encoding: chunked\r\n") &&
      wire.contains("contentFeatures.dlna.org: DLNA.ORG_OP=00;DLNA.ORG_CI=1;") &&
      client.state() == QAbstractSocket::ConnectedState && transcode->state() == State::Running,
      "Send the verified fragmented MP4 prefix while the final torrent piece remains unavailable");
  require(listener.withdrawTranscode(transcode), "Withdraw the live output link");
  wire += collect(client);
  require(!wire.endsWith("0\r\n\r\n") && whole("GET", auth).startsWith("HTTP/1.1 404 "),
      "End withdrawn partial delivery without claiming a complete HTTP body");
  require(transcode->cancel(), "Cancel the blocked verified input worker");
  waitFor([&] { return terminal(transcode->state()); });
  require(transcode->state() == State::Cancelled, "Join the unfinished media process after withdrawal");
  listener.stop();
  fixture.close();
  std::cout << "PASS authenticated fragmented MP4 over HTTP before verified torrent EOF and withdrawal\n";
}

void directoryCases() {
  using media::MediaBrowseMode;
  using media::MediaContentServer;
  using media::MediaDirectoryItem;
  Fixture fixture(QByteArray(16397, 't'));
  MediaContentServer listener(fixture.session);
  require(listener.start(0, {1s, 2s, 2}) && listener.publish(*fixture.input),
      "Publish held file into the active directory");
  const auto movies = listener.browse({3, MediaBrowseMode::DirectChildren, 0, 0});
  require(movies && movies->numberReturned == 1 && movies->totalMatches == 1 &&
      movies->didl.contains("<item id=\"6\" parentID=\"3\"") &&
      movies->didl.contains("<dc:title>sample.mkv</dc:title>") &&
      movies->didl.contains("size=\"16397\"") &&
      movies->didl.contains("http-get:*:video/x-matroska:*") &&
      movies->didl.contains("http-get:*:video/x-mkv:*"), "Browse maps original media aliases and exact file size");
  const auto downloads = listener.browse({4, MediaBrowseMode::DirectChildren, 0, 0});
  require(downloads && downloads->didl.contains("<item id=\"5\" parentID=\"4\"") &&
      downloads->updateId == movies->updateId, "Downloads and Movies point to one explicitly published file");
  const auto item = listener.browse({6, MediaBrowseMode::Metadata, 17, 1, true});
  require(item && item->didl.contains(">object.item.videoItem<") && item->numberReturned == 1,
      "Metadata resolves the item alias and original Xbox video class");
  const auto root = listener.browse({0, MediaBrowseMode::DirectChildren, 1, 2});
  require(root && root->numberReturned == 2 && root->totalMatches == 4 &&
      root->didl.indexOf(">Movies<") < root->didl.indexOf(">Music<") &&
      !root->didl.contains(">Downloads<") && !root->didl.contains(">Pictures<"), "Page the root in original order");

  QXmlStreamReader parsed(movies->didl);
  QByteArray uri;
  while (!parsed.atEnd()) {
    if (parsed.readNext() == QXmlStreamReader::StartElement && parsed.name() == u"res") {
      const auto address = parsed.readElementText();
      if (uri.isEmpty()) {
        uri = address.toLatin1();
      }
    }
  }
  require(!parsed.hasError() && !uri.isEmpty(), "Read the authoritative resource URL from DIDL");
  QTcpSocket client;
  client.setProxy(QNetworkProxy::NoProxy);
  const auto address = QUrl::fromEncoded(uri);
  client.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(address.port()));
  waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
  const auto request = "HEAD " + address.path().toLatin1() + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
  require(client.write(request) == request.size(), "Follow the advertised content resource");
  QByteArray response;
  waitFor([&] {
    response += client.readAll();
    return client.state() == QAbstractSocket::UnconnectedState;
  });
  require(response.startsWith("HTTP/1.1 200 ") && response.contains("Content-Length: 16397\r\n"),
      "DIDL resource resolves through the same owner and file geometry");

  require(listener.withdraw(*fixture.input), "Withdraw the directory source");
  const auto after = listener.browse({3, MediaBrowseMode::DirectChildren, 0, 0});
  require(after && after->totalMatches == 0 && after->updateId != movies->updateId &&
      after->didl.contains("DIDL-Lite"), "Revise the visible directory when its source disappears");

  std::vector<MediaDirectoryItem> generated;
  for (std::uint32_t id = 0; id < 130; ++id) {
    const auto name = QStringLiteral("show%1 & <title>.mkv").arg(id);
    generated.push_back({2 * id + 5, name, QByteArray(40, 'A'), id, 100, media::originalMediaType(name)});
  }
  const auto first = media::browseMediaDirectory(generated, 80, 17, {3, MediaBrowseMode::DirectChildren, 0, 0});
  require(first.numberReturned == 128 && first.totalMatches == 130 &&
      first.didl.contains("show2 &amp; &lt;title&gt;.mkv") &&
      first.didl.indexOf("show2 ") < first.didl.indexOf("show10 "), "Use original natural order, XML escaping and a bounded page");
  const auto tail = media::browseMediaDirectory(generated, 80, 17, {3, MediaBrowseMode::DirectChildren, 128, 8});
  require(tail.numberReturned == 2 && tail.totalMatches == 130 && tail.updateId == first.updateId,
      "Return the remainder with stable counts and update id");
  listener.stop();
  fixture.close();
  std::cout << "PASS DIDL directory, scoped publication, original sort and exact resource URL\n";
}
void soapCases() {
  Fixture fixture(QByteArray(16397, 't'));
  media::MediaContentServer server(fixture.session, media::MediaBasicCredentials{
      "viewer", QCryptographicHash::hash("generated", QCryptographicHash::Sha1)});
  require(server.start(0, {600ms, 2s, 2}) && server.publish(*fixture.input), "Start the owned SOAP and file endpoint");
  const QByteArray service = "urn:schemas-upnp-org:service:ContentDirectory:1";
  const QByteArray auth = "Authorization: Basic " + QByteArray("viewer:generated").toBase64() + "\r\n";
  const auto envelope = [&](QByteArrayView action, QByteArrayView args) {
    QByteArray xml = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:";
    const auto command = QByteArray(action.data(), action.size());
    xml += command + " xmlns:u=\"" + service + "\">";
    xml += args;
    xml += "</u:" + command + "></s:Body></s:Envelope>";
    return xml;
  };
  const auto post = [&](const QByteArray& action, const QByteArray& xml, bool useAuth = true) {
    QByteArray head = "POST /ContentDirectory/Control HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "User-Agent: Xbox Fixture\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n";
    if (useAuth) {
      head += auth;
    }
    head += "SOAPAction: \"" + service + '#' + action + "\"\r\nContent-Length: " +
        QByteArray::number(xml.size()) + "\r\n\r\n";
    return head + xml;
  };
  const auto exchange = [&](const QByteArray& request, bool fragment = false) {
    QTcpSocket client;
    client.setProxy(QNetworkProxy::NoProxy);
    client.connectToHost(QHostAddress::LocalHost, server.port());
    waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
    const auto split = fragment ? request.indexOf("\r\n\r\n") + 12 : request.size();
    require(client.write(request.constData(), split) == split, "Write the owned ContentDirectory request");
    if (fragment) {
      waitFor([&] { return client.bytesToWrite() == 0; });
      require(client.bytesAvailable() == 0 && client.write(request.constData() + split, request.size() - split) ==
          request.size() - split, "Wait for the declared body, then add the next fragment");
    }
    QByteArray received;
    waitFor([&] {
      received += client.readAll();
      return client.state() == QAbstractSocket::UnconnectedState;
    });
    return received;
  };
  const QByteArray args = "<ObjectID>3</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter>"
      "<StartingIndex>0</StartingIndex><RequestedCount>1</RequestedCount><SortCriteria>+dc:title</SortCriteria>";
  const auto browse = post("Browse", envelope("Browse", args));
  const auto response = exchange(browse, true);
  require(response.startsWith("HTTP/1.1 200 ") && response.contains("<NumberReturned>1</NumberReturned>") &&
      response.contains("<TotalMatches>1</TotalMatches>") && response.contains("&lt;DIDL-Lite") &&
      response.contains("object.item.videoItem&lt;") && response.contains("<UpdateID>"),
      "Authorized SOAP returns the current DIDL page and correct Xbox class");
  const auto update = exchange(post("GetSystemUpdateID", envelope("GetSystemUpdateID", {})));
  require(update.startsWith("HTTP/1.1 200 ") && update.contains("<Id>"), "Read the current system update ID");
  const auto sort = exchange(post("GetSortCapabilities", envelope("GetSortCapabilities", {})));
  require(sort.contains("<SortCaps>dc:title</SortCaps>"), "Return the original sort capability");
  const auto search = exchange(post("GetSearchCapabilities", envelope("GetSearchCapabilities", {})));
  require(search.startsWith("HTTP/1.1 200 ") && search.contains("<SearchCaps"), "Return the empty original search capability");
  require(exchange(post("Browse", envelope("Browse", args), false)).startsWith("HTTP/1.1 401 "),
      "Protect SOAP with the same configured Basic credentials");
  const auto mismatched = exchange(post("Browse", envelope("GetSystemUpdateID", {})));
  require(mismatched.startsWith("HTTP/1.1 500 ") && mismatched.contains("<errorCode>401</errorCode>"),
      "SOAPAction and XML must identify the same action");
  const auto duplicate = exchange(post("Browse", envelope("Browse", args + "<ObjectID>4</ObjectID>")));
  require(duplicate.startsWith("HTTP/1.1 500 ") && duplicate.contains("<errorCode>402</errorCode>"),
      "Reject duplicate control arguments");
  auto unsafe = envelope("Browse", args);
  unsafe.insert(21, "<!DOCTYPE input>");
  require(exchange(post("Browse", unsafe)).contains("<errorCode>402</errorCode>"), "Never accept document type declarations");
  require(exchange(browse + "tail").startsWith("HTTP/1.1 400 "), "Reject extra data beyond the request framing");
  require(exchange(post("Browse", QByteArray(16 * 1024 + 1, 'x'))).startsWith("HTTP/1.1 400 "),
      "Refuse an oversized control body before XML processing");
  server.stop();
  fixture.close();
  std::cout << "PASS ContentDirectory SOAP action match, current browse, capabilities, auth and bounded framing\n";
}
void connectionManagerCases() {
  Fixture fixture(QByteArray(16397, 't'));
  media::MediaContentServer server(fixture.session);
  require(server.start(0, {600ms, 2s, 2}), "Start the owned media control listener");
  const auto query = [&](const QByteArray& action, const QByteArray& args,
      const QByteArray& url = "/ConnectionManager/Control",
      const QByteArray& service = "urn:schemas-upnp-org:service:ConnectionManager:1") {
    const QByteArray body = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:" +
        action + " xmlns:u=\"" + service + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>";
    const QByteArray request = "POST " + url + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: text/xml\r\nSOAPAction: \"" +
        service + '#' + action + "\"\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    QTcpSocket client;
    client.setProxy(QNetworkProxy::NoProxy);
    client.connectToHost(QHostAddress::LocalHost, server.port());
    waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
    require(client.write(request) == request.size(), "Send the owned ConnectionManager control");
    QByteArray response;
    waitFor([&] {
      response += client.readAll();
      return client.state() == QAbstractSocket::UnconnectedState;
    });
    return response;
  };
  const auto protocols = query("GetProtocolInfo", {});
  require(protocols.startsWith("HTTP/1.1 200 ") && protocols.contains("u:GetProtocolInfoResponse") &&
      protocols.contains("http-get:*:video/x-matroska:*") && protocols.contains("http-get:*:video/x-mkv:*") &&
      protocols.contains("http-get:*:audio/mp4:*") && protocols.contains("<Sink") &&
      !protocols.contains("DLNA.ORG_CI"), "Advertise direct media types without transcoder flags");
  const auto ids = query("GetCurrentConnectionIDs", {});
  require(ids.startsWith("HTTP/1.1 200 ") && ids.contains("<ConnectionIDs"), "Report the original empty connection list");
  const auto details = query("GetCurrentConnectionInfo", "<ConnectionID>-1</ConnectionID>");
  require(details.startsWith("HTTP/1.1 200 ") && details.contains("<Status>Unknown</Status>") &&
      details.contains("<PeerConnectionID>-1</PeerConnectionID>"), "Preserve the original passive info response");
  require(query("GetCurrentConnectionInfo", {}).contains("<errorCode>402</errorCode>"),
      "ConnectionInfo requires a signed numeric ID");
  require(query("GetProtocolInfo", {}, "/ContentDirectory/Control").contains("<errorCode>401</errorCode>"),
      "The same action cannot cross into the directory service");
  require(query("GetProtocolInfo", "<Unexpected/>").contains("<errorCode>402</errorCode>"),
      "Capability actions have no inputs");
  server.stop();
  fixture.close();
  std::cout << "PASS ConnectionManager direct protocols, original passive info and service isolation\n";
}
void deviceDescriptionCases() {
  Fixture fixture(QByteArray(16397, 't'));
  media::MediaContentServer server(fixture.session);
  const media::MediaDeviceIdentity identity{
      QUuid(u"ddfb9663-ad52-4ab1-a935-ff94f6c6c642"), QStringLiteral("Library & Media <local>")};
  require(!server.configureDevice({QUuid{}, QStringLiteral("Missing UUID")}) &&
      !server.configureDevice({identity.id, QStringLiteral("Unusable\nname")}) &&
      server.configureDevice(identity) && server.start(0, {1s, 2s, 2}) && !server.configureDevice(identity),
      "Bind an explicit immutable device identity to its HTTP endpoint");
  const auto query = [&](const QByteArray& path, const QByteArray& method = "GET") {
    const QByteArray request = method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    QTcpSocket client;
    client.setProxy(QNetworkProxy::NoProxy);
    client.connectToHost(QHostAddress::LocalHost, server.port());
    waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
    require(client.write(request) == request.size(), "Send a device-description request");
    QByteArray response;
    waitFor([&] {
      response += client.readAll();
      return client.state() == QAbstractSocket::UnconnectedState;
    });
    return response;
  };
  const auto root = query("/RootDevice.xml");
  const auto split = root.indexOf("\r\n\r\n");
  require(split > 0 && root.startsWith("HTTP/1.1 200 ") && root.contains("urn:schemas-upnp-org:device:MediaServer:1") &&
      root.contains(identity.udn().toLatin1()) && root.contains("Library &amp; Media &lt;local&gt;") &&
      root.contains("<controlURL>/ConnectionManager/Control</controlURL>") &&
      root.contains("<controlURL>/ContentDirectory/Control</controlURL>"), "Describe only the real device and service URLs");
  QXmlStreamReader xml(root.mid(split + 4));
  while (!xml.atEnd()) { xml.readNext(); }
  require(!xml.hasError(), "Parse valid root device XML");
  const auto head = query("/RootDevice.xml", "HEAD");
  require(head.endsWith("\r\n\r\n") && head.contains("Content-Length: " + QByteArray::number(root.size() - split - 4) + "\r\n"),
      "HEAD describes the actual representation size");
  const auto directory = query("/ContentDirectory.xml");
  const auto connections = query("/ConnectionManager.xml");
  require(directory.startsWith("HTTP/1.1 200 ") && directory.contains("<name>Browse</name>") &&
      directory.contains("<name>GetSystemUpdateID</name>") && directory.contains("<relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable>") &&
      !directory.contains("<name>Search</name>"), "Advertise only allowed ContentDirectory actions");
  require(connections.startsWith("HTTP/1.1 200 ") && connections.contains("<name>GetProtocolInfo</name>") &&
      connections.contains("<name>GetCurrentConnectionInfo</name>"), "Describe the actual ConnectionManager actions");
  require(query("/Other.xml").startsWith("HTTP/1.1 404 "), "The owner provides only the named device documents");
  server.stop();
  fixture.close();
  std::cout << "PASS named device descriptions, service metadata, valid XML and representation HEAD\n";
}
void eventCases() {
  Fixture fixture(QByteArray(32784, 'r'));
  media::MediaContentServer server(fixture.session);
  QTcpServer callback;
  callback.setProxy(QNetworkProxy::NoProxy);
  require(callback.listen(QHostAddress::LocalHost, 0), "Start the owned callback receiver");
  const QByteArray callbackUrl = "http://127.0.0.1:" + QByteArray::number(callback.serverPort()) + "/notify";
  require(!server.allowEventCallback(QUrl("http://example.invalid:123/notify")) &&
      server.allowEventCallback(QUrl::fromEncoded(callbackUrl)) &&
      server.configureDevice({QUuid(u"ddfb9663-ad52-4ab1-a935-ff94f6c6c642"), QStringLiteral("Event test")}) &&
      server.start(0, {1s, 2s, 2}) && server.publish(*fixture.input), "Configure exactly one numeric callback endpoint");
  const auto control = [&](const QByteArray& method, const QByteArray& path, const QByteArray& headers) {
    const auto request = method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + headers + "\r\n";
    QTcpSocket client;
    client.setProxy(QNetworkProxy::NoProxy);
    client.connectToHost(QHostAddress::LocalHost, server.port());
    waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
    require(client.write(request) == request.size(), "Submit the event control request");
    QByteArray received;
    waitFor([&] {
      received += client.readAll();
      return client.state() == QAbstractSocket::UnconnectedState;
    });
    return received;
  };
  const auto readNotice = [&] {
    waitFor([&] { return callback.hasPendingConnections(); });
    std::unique_ptr<QTcpSocket> socket(callback.nextPendingConnection());
    QByteArray notice;
    waitFor([&] {
      notice += socket->readAll();
      return notice.contains("</e:propertyset>");
    });
    const QByteArray ack = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    require(socket->write(ack) == ack.size(), "Acknowledge the actual NOTIFY");
    socket->disconnectFromHost();
    waitFor([&] { return socket->state() == QAbstractSocket::UnconnectedState; });
    return notice;
  };
  const QByteArray directory = "/ContentDirectory/Event";
  const QByteArray connection = "/ConnectionManager/Event";
  require(control("SUBSCRIBE", directory, "NT: upnp:event\r\nCALLBACK: <" + callbackUrl + "2>\r\n")
      .startsWith("HTTP/1.1 412 "), "Never connect to unapproved callback paths");
  const auto subscribe = control("SUBSCRIBE", directory, "NT: upnp:event\r\nTIMEOUT: Second-10\r\nCALLBACK: <" +
      callbackUrl + ">\r\n");
  const auto sidStart = subscribe.indexOf("SID: ");
  require(subscribe.startsWith("HTTP/1.1 200 ") && subscribe.contains("TIMEOUT: Second-10\r\n") &&
      sidStart >= 0, "Accept the approved callback and issue an owned SID");
  const auto sid = subscribe.mid(sidStart + 5, subscribe.indexOf("\r\n", sidStart) - sidStart - 5);
  const auto initial = readNotice();
  require(initial.startsWith("NOTIFY /notify HTTP/1.1\r\n") && initial.contains("SID: " + sid + "\r\n") &&
      initial.contains("SEQ: 0\r\n") && initial.contains("<SystemUpdateID>"), "Send the initial directory state");
  require(control("SUBSCRIBE", connection, "SID: " + sid + "\r\n").startsWith("HTTP/1.1 412 "),
      "A SID cannot renew another service");
  require(control("SUBSCRIBE", directory, "SID: " + sid + "\r\nTIMEOUT: Second-20\r\n")
      .contains("TIMEOUT: Second-20\r\n"), "Renew the same current subscription");
  require(server.withdraw(*fixture.input), "Change the current published content set");
  const auto next = readNotice();
  require(next.contains("SEQ: 1\r\n") && next.contains("SID: " + sid + "\r\n") &&
      next.mid(next.indexOf("\r\n\r\n")) != initial.mid(initial.indexOf("\r\n\r\n")),
      "Publish the changed directory revision exactly on its own SID");
  require(control("UNSUBSCRIBE", directory, "SID: " + sid + "\r\n").startsWith("HTTP/1.1 200 ") &&
      control("SUBSCRIBE", directory, "SID: " + sid + "\r\n").startsWith("HTTP/1.1 412 "),
      "Remove the subscription on its explicit unsubscribe");

  const auto secondary = control("SUBSCRIBE", connection, "NT: upnp:event\r\nTIMEOUT: Second-1\r\nCALLBACK: <" +
      callbackUrl + ">\r\n");
  const auto secondSidAt = secondary.indexOf("SID: ");
  require(secondSidAt >= 0 && secondary.startsWith("HTTP/1.1 200 "), "Enroll the ConnectionManager subscription");
  const auto secondSid = secondary.mid(secondSidAt + 5, secondary.indexOf("\r\n", secondSidAt) - secondSidAt - 5);
  const auto initialConnections = readNotice();
  require(initialConnections.contains("SEQ: 0\r\n") && initialConnections.contains("<SourceProtocolInfo>") &&
      initialConnections.contains("<CurrentConnectionIDs"), "Send the passive connection state to the owned endpoint");
  QElapsedTimer expiry;
  expiry.start();
  waitFor([&] { return expiry.elapsed() >= 1100; });
  require(control("SUBSCRIBE", connection, "SID: " + secondSid + "\r\n").startsWith("HTTP/1.1 412 "),
      "An expired SID cannot be revived");
  server.stop();
  fixture.close();
  std::cout << "PASS owned GENA callback, sequence, revisions, renewal, unsubscribe and expiry\n";
}
void multicastCases() {
  const auto interface = QNetworkInterface::interfaceFromName("lo");
  const QHostAddress local(QHostAddress::LocalHost);
  const QHostAddress group("239.255.255.250");
  const auto endpoint = media::MediaLanEndpoint::fromInterface(interface.index(), local);
  require(endpoint && endpoint->available() && endpoint->accepts(local) && !endpoint->accepts(QHostAddress("127.0.0.2")) &&
      !media::MediaLanEndpoint::fromInterface(interface.index(), QHostAddress("10.203.87.1")),
      "Bind multicast to the selected address of the test interface");

  QUdpSocket observer;
  observer.setProxy(QNetworkProxy::NoProxy);
  require(observer.bind(group, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint) &&
      observer.joinMulticastGroup(group, interface), "Receive SSDP multicast on the owned interface");
  const auto udpPort = observer.localPort();
  Fixture fixture(QByteArray(16397, 'v'));
  media::MediaContentServer server(fixture.session);
  const media::MediaDeviceIdentity identity{
      QUuid(u"4575fbda-6b76-4161-8f9d-d8a6ff9ca6c6"), QStringLiteral("Owned multicast media")};
  require(server.configureLan(*endpoint) && server.configureDevice(identity) && server.start(0, {1s, 2s, 2}) &&
      server.startDiscovery(udpPort) && fixture.input && server.publish(*fixture.input),
      "Start the media owner and advertise its real device location");
  const auto authority = QByteArray("127.0.0.1:") + QByteArray::number(server.port());
  const auto location = "http://" + authority + "/RootDevice.xml";
  const auto udn = identity.udn().toLatin1();
  const auto announcements = [&](QByteArrayView type) {
    std::set<QByteArray> names;
    waitFor([&] {
      while (observer.hasPendingDatagrams()) {
        const auto packet = observer.receiveDatagram();
        const auto data = packet.data();
        if (!data.startsWith("NOTIFY * HTTP/1.1\r\n") || !data.contains(type)) { continue; }
        const auto start = data.indexOf("\r\nUSN: ");
        require(start >= 0 && packet.senderAddress() == local && packet.senderPort() == udpPort,
            "Advertise from the selected endpoint");
        const auto name = data.mid(start + 7, data.indexOf("\r\n", start + 7) - start - 7);
        require(name.startsWith(udn) && names.insert(name).second, "Advertise the matching identity once per target");
        require(type == "ssdp:alive" ? data.contains("LOCATION: " + location) : !data.contains("LOCATION:"),
            "Advertise the location only while the service is alive");
      }
      return names.size() == 5;
    });
    require(names.contains(udn) && names.contains(udn + "::upnp:rootdevice"), "Include both required device identities");
  };
  announcements("ssdp:alive");

  QUdpSocket requester;
  requester.setProxy(QNetworkProxy::NoProxy);
  require(requester.bind(local, 0), "Own a local active discovery socket");
  requester.setMulticastInterface(interface);
  requester.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
  const auto data = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:" + QByteArray::number(udpPort) +
      "\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: upnp:rootdevice\r\n\r\n";
  QNetworkDatagram search(data, group, udpPort);
  search.setInterfaceIndex(static_cast<uint>(interface.index()));
  require(requester.writeDatagram(search) == data.size(), "Send a multicast search from the test interface");
  waitFor([&] { return requester.hasPendingDatagrams(); });
  const auto response = requester.receiveDatagram();
  require(response.senderAddress() == local && response.senderPort() == udpPort &&
      response.data().startsWith("HTTP/1.1 200 OK\r\n") && response.data().contains("Location: " + location),
      "Return the exact HTTP location to the multicast searcher by unicast");

  const auto fetch = [&](const QByteArray& host, const QByteArray& path) {
    QTcpSocket client;
    client.setProxy(QNetworkProxy::NoProxy);
    client.connectToHost(local, server.port());
    waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; });
    const auto wire = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nRange: bytes=16381-16390\r\n\r\n";
    require(client.write(wire) == wire.size(), "Fetch from the selected HTTP listener");
    QByteArray result;
    waitFor([&] {
      result += client.readAll();
      return client.state() == QAbstractSocket::UnconnectedState;
    });
    return result;
  };
  require(fetch(authority, "/RootDevice.xml").contains("<UDN>" + udn + "</UDN>"), "Resolve the advertised device identity");
  require(fetch("wrong.example:443", "/RootDevice.xml").startsWith("HTTP/1.1 400 "), "Bind Host to the selected interface and port");
  const auto url = QUrl::fromEncoded(server.publishedUrl(*fixture.input));
  require(url.authority().toLatin1() == authority && fetch(authority, url.path().toLatin1()).endsWith(QByteArray(10, 'v')),
      "Serve the published bytes from the advertised authority");
  server.stop();
  announcements("ssdp:byebye");
  require(server.discoveryPort() == 0 && server.port() == 0, "Retire HTTP and multicast together");
  fixture.close();
  std::cout << "PASS owned multicast alive, byebye, SSDP search, HTTP authority and verified resource\n";
}

void discoveryCases() {
  Fixture fixture(QByteArray(16397, 'v'));
  media::MediaContentServer server(fixture.session);
  const media::MediaDeviceIdentity identity{
      QUuid(u"ddfb9663-ad52-4ab1-a935-ff94f6c6c642"), QStringLiteral("Owned SSDP Media Server")};
  require(!server.startDiscovery() && server.configureDevice(identity) &&
      server.start(0, {1s, 2s, 2}) && server.startDiscovery() && !server.startDiscovery(),
      "Discovery binds only for an owned running HTTP description");
  const auto port = server.discoveryPort();
  require(port != 0, "Own a local UDP search endpoint");
  QUdpSocket client;
  require(client.bind(QHostAddress::LocalHost, 0), "Own a loopback search socket");
  const auto request = [](const QByteArray& target, const QByteArray& mx = "1") {
    return "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: " +
        mx + "\r\nST: " + target + "\r\n\r\n";
  };
  const auto search = [&](const QByteArray& wire) {
    require(client.writeDatagram(wire, QHostAddress::LocalHost, port) == wire.size(), "Submit the owned SSDP search");
  };
  const auto header = [](const QByteArray& response, const QByteArray& key) {
    const auto first = response.indexOf(key);
    return first < 0 ? QByteArray{} : response.mid(first + key.size(), response.indexOf("\r\n", first) - first - key.size());
  };
  search(request("upnp:rootdevice", "0"));
  search(request("unknown:service"));
  search(request("upnp:rootdevice"));
  waitFor([&] { return client.hasPendingDatagrams(); });
  const auto packet = client.receiveDatagram();
  const auto response = packet.data();
  const auto udn = identity.udn().toLatin1();
  const auto address = header(response, "Location: ");
  require(packet.senderAddress() == QHostAddress::LocalHost && packet.senderPort() == port &&
      response.startsWith("HTTP/1.1 200 OK\r\n") && header(response, "ST: ") == "upnp:rootdevice" &&
      header(response, "USN: ") == udn + "::upnp:rootdevice" &&
      header(response, "Cache-Control: ") == "max-age=3600", "Return the original device location and exact root identity");
  const auto url = QUrl::fromEncoded(address);
  require(url.host() == "127.0.0.1" && url.port() == server.port() && url.path() == "/RootDevice.xml",
      "Bind the advertised location to the real owned HTTP listener");
  {
    QTcpSocket http;
    http.setProxy(QNetworkProxy::NoProxy);
    http.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(url.port()));
    waitFor([&] { return http.state() == QAbstractSocket::ConnectedState; });
    const QByteArray get = "GET " + url.path().toLatin1() + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    require(http.write(get) == get.size(), "Follow the SSDP location");
    QByteArray xml;
    waitFor([&] {
      xml += http.readAll();
      return http.state() == QAbstractSocket::UnconnectedState;
    });
    require(xml.startsWith("HTTP/1.1 200 ") && xml.contains("<UDN>" + udn + "</UDN>"), "The location has the same UDN");
  }
  search(request("ssdp:all"));
  std::set<QByteArray> targets;
  waitFor([&] {
    while (client.hasPendingDatagrams()) {
      const auto reply = client.receiveDatagram().data();
      require(header(reply, "Location: ") == address && targets.insert(header(reply, "ST: ")).second,
          "Return each discovery target once at the same location");
    }
    return targets.size() == 5;
  });
  require(targets.contains(udn) && targets.contains("urn:schemas-upnp-org:service:ContentDirectory:1") &&
      targets.contains("urn:schemas-upnp-org:service:ConnectionManager:1"), "Expose the current service targets");
  server.stop();
  require(server.discoveryPort() == 0, "Close discovery together with its HTTP location");
  fixture.close();
  std::cout << "PASS owned SSDP search targets and exact HTTP device location\n";
}
void rendererDiscoveryCases() {
  const QHostAddress local(QHostAddress::LocalHost);
  QUdpSocket ssdp;
  QUdpSocket differentSender;
  QTcpServer http;
  for (auto* socket : {&ssdp, &differentSender}) {
    socket->setProxy(QNetworkProxy::NoProxy);
    require(socket->bind(local, 0), "Own a renderer discovery UDP endpoint");
  }
  http.setProxy(QNetworkProxy::NoProxy);
  require(http.listen(local, 0), "Own the renderer description endpoint");
  const QUuid id(u"55969992-47ba-4e88-a65a-e5c3fbedda40");
  const auto url = "http://127.0.0.1:" + QByteArray::number(http.serverPort());
  const auto udn = "uuid:" + id.toString(QUuid::WithoutBraces).toLatin1();
  const QByteArray type("urn:schemas-upnp-org:device:MediaRenderer:1");
  const auto findDevice = [&](const QByteArray& base, const QByteArray& xmlId) {
    return "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">" + base + "<device><deviceType>" + type +
        "</deviceType><UDN>" + xmlId + "</UDN><friendlyName>Kitchen &amp; TV</friendlyName><serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
        "<controlURL>control</controlURL></service><service>"
        "<serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"
        "<controlURL>/connect</controlURL></service></serviceList></device></root>";
  };
  media::MediaRendererDiscovery discovery;
  const auto start = [&] {
    require(discovery.start(ssdp.localPort(), 2s), "Start an explicitly owned renderer discovery");
    waitFor([&] { return ssdp.hasPendingDatagrams(); });
    const auto search = ssdp.receiveDatagram();
    require(search.senderAddress() == local && search.data().contains("\r\nST: " + type + "\r\n"),
        "Search the original MediaRenderer device type");
    return search;
  };
  const auto announce = [&](QUdpSocket& socket, const QNetworkDatagram& search, const QByteArray& location) {
    const auto wire = "HTTP/1.1 200 OK\r\nST: " + type + "\r\nUSN: " + udn + "::" + type +
        "\r\nLocation: " + location + "\r\n\r\n";
    require(socket.writeDatagram(wire, local, search.senderPort()) == wire.size(), "Return a generated SSDP response");
  };
  const auto serve = [&](const QByteArray& data, bool redirect = false) {
    const auto search = start();
    announce(differentSender, search, url + "/unexpected");
    announce(ssdp, search, "http://localhost:" + QByteArray::number(http.serverPort()) + "/unexpected");
    announce(ssdp, search, url + "/device/setup.xml");
    waitFor([&] { return http.hasPendingConnections(); });
    std::unique_ptr<QTcpSocket> connection(http.nextPendingConnection());
    QByteArray request;
    waitFor([&] { request += connection->readAll(); return request.endsWith("\r\n\r\n"); });
    require(request.startsWith("GET /device/setup.xml HTTP/1.1\r\n"), "Fetch only the selected numeric peer and location");
    const auto wire = redirect ? QByteArray("HTTP/1.1 302 Found\r\nLocation: /unexpected\r\nContent-Length: 0\r\n") :
        "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " + QByteArray::number(data.size()) + "\r\n";
    connection->write(wire + "Connection: close\r\n\r\n" + data);
    connection->disconnectFromHost();
    waitFor([&] { return !discovery.active(); });
    require(!http.hasPendingConnections(), "Do not follow a renderer description redirect");
  };
  serve(findDevice({}, udn));
  require(discovery.devices().size() == 1 && discovery.devices().front().id == id && discovery.devices().front().name == u"Kitchen & TV" &&
      discovery.devices().front().avTransport.toEncoded() == url + "/device/control" &&
      discovery.devices().front().connectionManager.toEncoded() == url + "/connect", "Own the original renderer identity and service paths");
  serve(findDevice("<URLBase>http://127.0.0.1:12345/</URLBase>", udn));
  require(discovery.devices().empty(), "Reject an XML URLBase on a different origin");
  serve(findDevice({}, "uuid:11111111-2222-3333-4444-555555555555"));
  require(discovery.devices().empty(), "Require SSDP and HTTP description to identify the same renderer");
  serve("<!DOCTYPE root>" + findDevice({}, udn));
  require(discovery.devices().empty(), "Reject a renderer description that declares a DTD");
  serve({});
  require(discovery.devices().empty(), "Reject an empty description");
  serve({}, true);
  require(discovery.devices().empty(), "Do not promote a redirect to a renderer description");
  serve(QByteArray(50 * 1024, 'x'));
  require(discovery.devices().empty(), "Bound the renderer description body");
  start();
  discovery.cancel();
  require(!discovery.active() && discovery.devices().empty(), "Cancel the discovery owner before an SSDP response");
  std::cout << "PASS owned renderer SSDP identity, description and exact control URLs\n";
}


void rendererLanCases() {
  const QHostAddress local(QHostAddress::LocalHost);
  const QHostAddress group("239.255.255.250");
  const auto interface = QNetworkInterface::interfaceFromName("lo");
  const auto endpoint = media::MediaLanEndpoint::fromInterface(interface.index(), local);
  require(endpoint.has_value(), "Own the exact loopback interface for multicast renderer discovery");
  QUdpSocket receiver;
  QUdpSocket peer;
  QUdpSocket foreign;
  QTcpServer descriptions;
  receiver.setProxy(QNetworkProxy::NoProxy);
  peer.setProxy(QNetworkProxy::NoProxy);
  foreign.setProxy(QNetworkProxy::NoProxy);
  descriptions.setProxy(QNetworkProxy::NoProxy);
  require(receiver.bind(group, 0) && receiver.joinMulticastGroup(group, interface) && peer.bind(local, 0) &&
      foreign.bind(QHostAddress("127.0.0.2"), 0) && descriptions.listen(local, 0), "Own two renderer responders");
  const QByteArray rendererType("urn:schemas-upnp-org:device:MediaRenderer:1");
  const QByteArray first("uuid:ec81aa26-b850-4858-b259-ad6c58450ac8");
  const QByteArray second("uuid:9c6ac250-8a18-4c1a-86fe-22ea02abc171");
  const QByteArray silent("uuid:ac8fce08-24fc-4766-9cbd-626a555dc466");
  const auto url = "http://127.0.0.1:" + QByteArray::number(descriptions.serverPort());
  const auto payload = [&](const QByteArray& id, const QByteArray& label) {
    return "<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device><deviceType>" + rendererType +
        "</deviceType><UDN>" + id + "</UDN><friendlyName>" + label + "</friendlyName><serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType><controlURL>/av</controlURL></service>"
        "<service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType><controlURL>/cm</controlURL></service>"
        "</serviceList></device></root>";
  };
  std::vector<QByteArray> requested;
  QObject::connect(&descriptions, &QTcpServer::newConnection, &descriptions, [&] {
    while (auto* socket = descriptions.nextPendingConnection()) {
      auto input = std::make_shared<QByteArray>();
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, &descriptions, [&, socket, input] {
        *input += socket->readAll();
        if (!input->endsWith("\r\n\r\n")) { return; }
        const auto path = input->split(' ').at(1);
        requested.push_back(path);
        if (path == "/silent.xml") { return; }
        const auto body = path == "/first.xml" ? payload(first, "First TV") : payload(second, "Second TV");
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " + QByteArray::number(body.size()) +
            "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
      });
    }
  });

  media::MediaRendererDiscovery discovery;
  require(discovery.startLan(*endpoint, 2500ms, receiver.localPort()), "Send an actual multicast renderer query");
  waitFor([&] { return receiver.hasPendingDatagrams(); });
  const auto search = receiver.receiveDatagram();
  require(search.senderAddress() == local && search.data().contains("ST: " + rendererType), "Observe the selected-interface search");
  const auto respond = [&](QUdpSocket& socket, const QByteArray& id, const QByteArray& path) {
    const auto wire = "HTTP/1.1 200 OK\r\nST: " + rendererType + "\r\nUSN: " + id + "::" + rendererType +
        "\r\nLocation: " + url + path + "\r\n\r\n";
    require(socket.writeDatagram(wire, local, search.senderPort()) == wire.size(), "Answer the generated multicast search");
  };
  respond(foreign, "uuid:aaccaa26-b850-4858-b259-ad6c58450abc", "/foreign.xml");
  respond(peer, silent, "/silent.xml");
  respond(peer, first, "/first.xml");
  respond(peer, first, "/duplicate.xml");
  respond(peer, second, "/second.xml");
  waitFor([&] { return !discovery.active(); });
  require(discovery.devices().size() == 2 && discovery.devices()[0].name == u"First TV" &&
      discovery.devices()[1].name == u"Second TV" &&
      requested == std::vector<QByteArray>{"/silent.xml", "/first.xml", "/second.xml"},
      "Bound a silent description, deduplicate identities and retain both real response endpoints");
  discovery.cancel();
  require(discovery.devices().empty(), "Retire discovery results with the worker");
  std::cout << "PASS owned selected-interface renderer multicast, two descriptions and silent-peer skip\n";
}


class RendererFixture final : public QObject {
public:
  enum class Mode { Reply, Hold, HoldSet, Redirect };
  QTcpServer listener;
  Mode mode = Mode::Reply;
  bool rejectPreparation = false;
  std::vector<std::pair<QByteArray, QByteArray>> commands;
  QPointer<QTcpSocket> held;

  RendererFixture() {
    listener.setProxy(QNetworkProxy::NoProxy);
    require(listener.listen(QHostAddress::LocalHost, 0), "Start the generated AVTransport endpoint");
    connect(&listener, &QTcpServer::newConnection, this, [this] {
      while (auto* socket = listener.nextPendingConnection()) {
        auto input = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, input] {
          *input += socket->readAll();
          const auto split = input->indexOf("\r\n\r\n");
          if (split < 0) {
            return;
          }
          const auto headers = input->first(split).toLower();
          const auto header = headers.indexOf("content-length: ");
          if (header < 0) {
            return;
          }
          const auto digits = header + QByteArrayView("Content-Length: ").size();
          bool lengthValid = false;
          const auto bodySize = input->mid(digits, input->indexOf("\r\n", digits) - digits).toLongLong(&lengthValid);
          if (!lengthValid || input->size() - split - 4 < bodySize) {
            return;
          }
          const auto actionStart = headers.indexOf("soapaction: ");
          if (actionStart < 0 || (!input->startsWith("POST /control HTTP/1.1\r\n") &&
              !input->startsWith("POST /connect HTTP/1.1\r\n"))) {
            socket->abort();
            return;
          }
          const auto hash = input->indexOf('#', actionStart);
          const auto quote = input->indexOf('"', hash);
          commands.push_back({input->mid(hash + 1, quote - hash - 1),
              input->mid(split + 4, static_cast<qsizetype>(bodySize))});
          socket->disconnect(this);
          if (mode == Mode::Hold || (mode == Mode::HoldSet && commands.back().first == "SetAVTransportURI")) {
            held = socket;
          } else if (mode == Mode::Redirect) {
            socket->write("HTTP/1.1 302 Found\r\nLocation: /redirect\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
          } else {
            acknowledge(*socket, commands.back().first);
          }
        });
      }
    });
  }

  QUrl control() const {
    return QUrl::fromEncoded("http://127.0.0.1:" + QByteArray::number(listener.serverPort()) + "/control");
  }

  QUrl connections() const {
    auto url = control();
    url.setPath(QStringLiteral("/connect"));
    return url;
  }

  void release() {
    require(held && !commands.empty(), "Have the pending renderer reply");
    auto* socket = held.data();
    held.clear();
    acknowledge(*socket, commands.back().first);
  }

private:
  void acknowledge(QTcpSocket& socket, const QByteArray& action) {
    if (rejectPreparation && action == "PrepareForConnection") {
      socket.write("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      socket.disconnectFromHost();
      return;
    }
    QByteArray fields;
    if (action == "GetProtocolInfo") { fields = "<Sink>http-get:*:video/mp4:*</Sink>"; }
    if (action == "PrepareForConnection") { fields = "<ConnectionID>9</ConnectionID><AVTransportID>7</AVTransportID><RcsID>2</RcsID>"; }
    if (action == "GetCurrentConnectionIDs") { fields = "<ConnectionIDs>4,5</ConnectionIDs>"; }
    const auto manager = !fields.isEmpty() || action == "ConnectionComplete";
    const auto service = manager ? "ConnectionManager:1" : "AVTransport:1";
    const auto tag = action + "Response";
    const auto xml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:" +
        tag + " xmlns:u=\"urn:schemas-upnp-org:service:" + service + "\">" + fields + "</u:" + tag + "></s:Body></s:Envelope>";
    socket.write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " + QByteArray::number(xml.size()) +
        "\r\nConnection: close\r\n\r\n" + xml);
    socket.disconnectFromHost();
  }
};

void rendererCases() {
  Fixture fixture(QByteArray(16397, 'z'));
  media::MediaContentServer server(fixture.session);
  require(server.start(0, {1s, 2s, 2}) && server.publish(*fixture.input), "Publish one current renderer source");
  const auto page = server.browse({5, media::MediaBrowseMode::Metadata, 0, 0});
  require(page && page->numberReturned == 1, "Supply the actual DIDL for the published file");
  const auto resource = QUrl::fromEncoded("http://127.0.0.1:" + QByteArray::number(server.port()) + "/Content/" +
      QByteArray::fromStdString(fixture.input->hash.hex()) + "-0.mkv");
  using State = media::MediaRendererTransport::State;
  {
    RendererFixture renderer;
    media::MediaRendererTransport transport(renderer.control());
    require(transport.play(resource, page->didl, 0, [] { return true; }, 2s), "Start the selected transport");
    waitFor([&] { return transport.state() == State::Playing; });
    require(renderer.commands.size() == 2 && renderer.commands[0].first == "SetAVTransportURI" &&
        renderer.commands[0].second.contains("<CurrentURI>" + resource.toEncoded() + "</CurrentURI>") &&
        renderer.commands[0].second.contains("&lt;DIDL-Lite") && renderer.commands[1].first == "Play" &&
        renderer.commands[1].second.contains("<Speed>1</Speed>"), "Preserve original SetURI then Play order");
    require(transport.stop(2s), "Issue a typed Stop after acknowledged play");
    waitFor([&] { return transport.state() == State::Stopped; });
    require(renderer.commands.back().first == "Stop", "Address Stop to the same AVTransport instance");
  }
  {
    RendererFixture renderer;
    renderer.mode = RendererFixture::Mode::Hold;
    media::MediaRendererTransport transport(renderer.control());
    bool live = true;
    require(transport.play(resource, page->didl, 0, [&] { return live; }, 2s), "Begin a revocable renderer operation");
    waitFor([&] { return renderer.held; });
    live = false;
    renderer.release();
    waitFor([&] { return transport.state() == State::Failed; });
    require(renderer.commands.size() == 1, "Do not send Play after source retirement");
  }
  {
    RendererFixture renderer;
    renderer.mode = RendererFixture::Mode::Hold;
    media::MediaRendererTransport transport(renderer.control());
    require(transport.play(resource, page->didl, 0, [] { return true; }, 2s), "Start the cancellable transport");
    waitFor([&] { return renderer.held; });
    require(transport.cancel() && transport.state() == State::Cancelled, "Cancel the in-flight renderer operation");
    require(renderer.commands.size() == 1, "Cancellation does not chain a Play");
  }
  {
    RendererFixture renderer;
    renderer.mode = RendererFixture::Mode::Redirect;
    media::MediaRendererTransport transport(renderer.control());
    require(transport.play(resource, page->didl, 0, [] { return true; }, 2s), "Begin the renderer redirect control");
    waitFor([&] { return transport.state() == State::Failed; });
    require(renderer.commands.size() == 1, "Do not follow a renderer redirect");
  }
  {
    RendererFixture renderer;
    renderer.mode = RendererFixture::Mode::Hold;
    media::MediaRendererTransport transport(renderer.control());
    require(transport.play(resource, page->didl, 0, [] { return true; }, 400ms), "Begin the silent renderer control");
    waitFor([&] { return transport.state() == State::Failed; });
    require(renderer.commands.size() == 1, "A silent renderer times out before Play");
  }
  server.stop();
  fixture.close();
  std::cout << "PASS owned AVTransport SetURI, Play, Stop, retirement, cancellation, redirect and timeout\n";
}
void rendererNegotiationCases() {
  Fixture fixture(QByteArray(16397, 'z'));
  media::MediaContentServer server(fixture.session);
  require(server.start(0, {1s, 2s, 2}) && fixture.input && server.publish(*fixture.input),
      "Publish one current negotiated renderer source");
  const auto page = server.browse({5, media::MediaBrowseMode::Metadata, 0, 0});
  const auto resource = QUrl::fromEncoded(server.publishedUrl(*fixture.input));
  require(page && page->numberReturned == 1 && resource.isValid(), "Use a current resource and its DIDL");
  const QStringList types{QStringLiteral("video/x-matroska"), QStringLiteral("video/mp4")};
  using State = media::MediaRendererTransport::State;
  {
    RendererFixture renderer;
    media::MediaRendererTransport transport(renderer.control(), renderer.connections());
    require(transport.playNegotiated(resource, page->didl, types, [] { return true; }, 2s), "Begin a negotiated play");
    waitFor([&] { return transport.state() == State::Playing; });
    const auto& calls = renderer.commands;
    require(calls.size() == 4 && calls[0].first == "GetProtocolInfo" && calls[1].first == "PrepareForConnection" &&
        calls[1].second.contains("<RemoteProtocolInfo>http-get:*:video/mp4:*</RemoteProtocolInfo>") &&
        calls[1].second.contains("<PeerConnectionID>-1</PeerConnectionID>") &&
        calls[1].second.contains("<Direction>Input</Direction>") && calls[2].first == "SetAVTransportURI" &&
        calls[2].second.contains("<InstanceID>7</InstanceID>") && calls[3].first == "Play" &&
        calls[3].second.contains("<InstanceID>7</InstanceID>"), "Select the original sink type and negotiated instance");
    require(transport.stop(2s), "Stop the negotiated renderer");
    waitFor([&] { return transport.state() == State::Stopped; });
    require(calls.size() == 6 && calls[4].first == "Stop" && calls[4].second.contains("<InstanceID>7</InstanceID>") &&
        calls[5].first == "ConnectionComplete" && calls[5].second.contains("<ConnectionID>9</ConnectionID>"),
        "Retire only the prepared renderer connection after stop");
  }
  {
    RendererFixture renderer;
    renderer.rejectPreparation = true;
    media::MediaRendererTransport transport(renderer.control(), renderer.connections());
    require(transport.playNegotiated(resource, page->didl, types, [] { return true; }, 2s), "Begin a fallback renderer play");
    waitFor([&] { return transport.state() == State::Playing; });
    const auto& calls = renderer.commands;
    require(calls.size() == 5 && calls[2].first == "GetCurrentConnectionIDs" && calls[3].first == "SetAVTransportURI" &&
        calls[3].second.contains("<InstanceID>0</InstanceID>"), "Use the original default instance after a Prepare fault");
    require(transport.stop(2s), "Stop the fallback renderer instance");
    waitFor([&] { return transport.state() == State::Stopped; });
    require(calls.size() == 6 && calls.back().first == "Stop", "Preserve an existing renderer connection during fallback");
  }
  {
    RendererFixture renderer;
    renderer.mode = RendererFixture::Mode::HoldSet;
    media::MediaRendererTransport transport(renderer.control(), renderer.connections());
    bool live = true;
    require(transport.playNegotiated(resource, page->didl, types, [&] { return live; }, 2s), "Begin a revocable negotiated play");
    waitFor([&] { return renderer.held; });
    live = false;
    renderer.release();
    waitFor([&] { return transport.state() == State::Failed; });
    require(renderer.commands.size() == 4 && renderer.commands.back().first == "ConnectionComplete",
        "A retired source releases its prepared connection before Play");
  }
  fixture.close();
  std::cout << "PASS owned renderer connection negotiation, fallback and prepared connection release\n";
}


} // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  if (argc > 1 && QByteArrayView(argv[1]) == "-nostdin") {
    return fixtureChild(QString::fromLocal8Bit(argv[argc - 1]));
  }
  try {
    if (argc == 1) {
      fixtureCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--ranges") {
      rangeCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--wire") {
      wireCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--routes") {
      routeCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--listener") {
      listenerCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--transcoded-http") {
      Fixture fixture(QByteArray(16397, 't'));
      media::FfmpegJob job;
      job.program = program(QCoreApplication::applicationFilePath());
      job.output = media::FfmpegContainer::Mp4;
      const auto bytes = transcodedHttp(fixture, job);
      require(bytes == QCryptographicHash::hash(QByteArray(16397, 't'), QCryptographicHash::Sha256),
          "The HTTP fixture delivers the joined child's digest of the verified torrent");
      fixture.close();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--directory") {
      directoryCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--soap") {
      soapCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--connections") {
      connectionManagerCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--device") {
      deviceDescriptionCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--events") {
      eventCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--ssdp") {
      discoveryCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--ssdp-multicast") {
      multicastCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--renderer") {
      rendererCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--renderer-negotiation") {
      rendererNegotiationCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--renderer-discovery") {
      rendererDiscoveryCases();
    } else if (argc == 2 && QByteArrayView(argv[1]) == "--renderer-multicast") {
      rendererLanCases();
    } else if (argc == 5 && QByteArrayView(argv[1]) == "--progressive-http") {
      media::FfmpegJob job;
      job.program = {QString::fromLocal8Bit(argv[2]), QByteArray::fromHex(argv[3])};
      job.output = media::FfmpegContainer::Mp4;
      job.geometry = media::TranscodeVideoGeometry{96, 64, 0, 0, std::nullopt};
      progressiveHttp(test::read(QString::fromLocal8Bit(argv[4])), std::move(job));
    } else if (argc == 5 || (argc == 6 && (QByteArrayView(argv[1]) == "--profile-geometry" ||
        QByteArrayView(argv[1]) == "--serve-transcoded"))) {
      const bool serve = argc == 6 && QByteArrayView(argv[1]) == "--serve-transcoded";
      const bool useProfile = argc == 6 && !serve;
      const int program = argc == 6 ? 2 : 1;
      Fixture fixture(test::read(QString::fromLocal8Bit(argv[program + 2])));
      media::FfmpegJob job;
      job.program = {QString::fromLocal8Bit(argv[program]), QByteArray::fromHex(argv[program + 1])};
      job.output = media::FfmpegContainer::Mp4;
      job.geometry = media::TranscodeVideoGeometry{80, 48, 0, 0, std::nullopt};
      if (useProfile) {
        const auto profile = media::readTranscodeProfile("name=Generated geometric profile\nvideo-width=96\nvideo-height=80\n"
            "aspect-ratios=4:3\nauto-padding=true\npadding-mod=2\nvideo-args=-metadata title=ignored\n", false);
        require(profile.has_value(), "Read the original-style profile geometry");
        job.geometry = media::transcodeVideoGeometry(*profile, 96, 64);
        require(job.geometry && job.geometry->width == 96 && job.geometry->height == 72 && job.geometry->padY == 4,
            "Select the original geometry before starting the pinned worker");
      }
      job.deadline = 8s;
      const auto destination = QString::fromLocal8Bit(argv[program + 3]);
      if (serve) {
        test::write(destination, transcodedHttp(fixture, job));
      } else {
        media::TorrentTranscode transfer(fixture.session, *fixture.input);
        require(bool(transfer.start(job)), "Start digest-checked FFmpeg on the admitted torrent");
        waitFor([&] { return terminal(transfer.state()); }, 10s);
        require(transfer.state() == media::FfmpegJobState::Produced, transfer.diagnostics().toStdString().c_str());
        require(QFile::copy(transfer.resultFile(), destination), "Export generated media for inspection");
      }
      fixture.close();
    } else {
      return 2;
    }
    std::cout << "PASS verified torrent media input and joined child output\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
