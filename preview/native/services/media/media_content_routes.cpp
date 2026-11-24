// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_content_routes.hpp"
#include "torrent_transcode.hpp"
#include <QUuid>
#include "media_directory_control.hpp"

#include <QCryptographicHash>
#include <QUrl>
#include <algorithm>
#include <charconv>
#include <limits>
#include <openssl/crypto.h>

namespace biglybtc::media {
namespace {
using Error = MediaRequestError;
constexpr auto maximumResources = 4096;

char lowerAscii(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool equalsAscii(std::string_view left, std::string_view right) {
  return std::ranges::equal(left, right, [](char a, char b) { return lowerAscii(a) == lowerAscii(b); });
}

bool headerToken(char value) {
  const auto lower = lowerAscii(value);
  return (lower >= 'a' && lower <= 'z') || (value >= '0' && value <= '9') ||
      std::string_view("!#$%&'*+-.^_`|~").find(value) != std::string_view::npos;
}

std::string_view trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t");
  return first == value.npos ? std::string_view{} : value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

bool localAuthority(std::string_view value, std::string_view lanAuthority) {
  if (!lanAuthority.empty()) {
    return value == lanAuthority;
  }
  const auto end = value.starts_with('[') ? value.find(']') : value.find(':');
  if (value.starts_with('[') && end == value.npos) {
    return false;
  }
  const auto nameEnd = value.starts_with('[') ? end + 1 : end;
  const auto name = value.substr(0, nameEnd);
  if (!equalsAscii(name, "localhost") && name != "127.0.0.1" && name != "[::1]") {
    return false;
  }
  if (name.size() == value.size()) {
    return true;
  }
  const auto suffix = value.substr(name.size());
  if (!suffix.starts_with(':') || suffix.size() == 1) {
    return false;
  }
  std::uint16_t port = 0;
  const auto read = std::from_chars(suffix.data() + 1, suffix.data() + suffix.size(), port);
  return read.ec == std::errc{} && read.ptr == suffix.data() + suffix.size() && port != 0;
}

struct Request {
  std::optional<MediaHttpMethod> method;
  std::optional<MediaEventMethod> eventMethod;
  std::string_view target;
  std::optional<std::string_view> authorization;
  std::optional<std::string_view> range;
  std::optional<std::string_view> soapAction;
  std::optional<std::string_view> contentType;
  std::optional<std::string_view> userAgent;
  std::optional<qsizetype> contentLength;
  std::optional<std::string_view> sid;
  std::optional<std::string_view> callback;
  std::optional<std::string_view> notificationType;
  std::optional<std::string_view> timeout;
  bool http11;
};

std::expected<Request, Error> parseRequest(std::string_view request, std::string_view lanAuthority) {
  if (request.size() < 4 || request.size() > maximumMediaHttpHeader ||
      request.find("\r\n\r\n") != request.size() - 4) {
    return std::unexpected(Error::BadRequest);
  }
  const auto lineEnd = request.find("\r\n");
  const auto line = request.substr(0, lineEnd);
  const auto methodEnd = line.find(' ');
  const auto targetEnd = line.rfind(' ');
  if (methodEnd == line.npos || methodEnd == targetEnd || targetEnd > 2048 ||
      (line.substr(targetEnd + 1) != "HTTP/1.1" && line.substr(targetEnd + 1) != "HTTP/1.0")) {
    return std::unexpected(Error::BadRequest);
  }
  const auto method = line.substr(0, methodEnd);
  Request result{};
  result.http11 = line.substr(targetEnd + 1) == "HTTP/1.1";
  if (method == "GET" || method == "HEAD") {
    result.method = method == "GET" ? MediaHttpMethod::Get : MediaHttpMethod::Head;
  } else if (method == "SUBSCRIBE" || method == "UNSUBSCRIBE") {
    result.eventMethod = method == "SUBSCRIBE" ? MediaEventMethod::Subscribe : MediaEventMethod::Unsubscribe;
  } else if (method != "POST") {
    return std::unexpected(Error::Unsupported);
  }
  result.target = line.substr(methodEnd + 1, targetEnd - methodEnd - 1);
  if (result.target.empty() || !std::ranges::all_of(result.target, [](unsigned char value) {
        return value >= 33 && value <= 126 && value != '#';
      })) {
    return std::unexpected(Error::BadRequest);
  }

  bool host = false;
  unsigned count = 0;
  for (auto headers = request.substr(lineEnd + 2); headers != "\r\n";) {
    const auto end = headers.find("\r\n");
    const auto header = headers.substr(0, end);
    const auto colon = header.find(':');
    const auto key = header.substr(0, colon);
    if (++count > 64 || end == headers.npos || colon == header.npos || key.empty() ||
        !std::ranges::all_of(key, headerToken)) {
      return std::unexpected(Error::BadRequest);
    }
    const auto value = trim(header.substr(colon + 1));
    if (!std::ranges::all_of(value, [](unsigned char ch) { return ch == '\t' || (ch >= 32 && ch != 127); })) {
      return std::unexpected(Error::BadRequest);
    }
    if (equalsAscii(key, "authorization")) {
      if (result.authorization) {
        return std::unexpected(Error::BadRequest);
      }
      result.authorization = value;
    } else if (equalsAscii(key, "range")) {
      if (result.range) {
        return std::unexpected(Error::BadRequest);
      }
      result.range = value;
    } else if (equalsAscii(key, "host")) {
      if (host || !localAuthority(value, lanAuthority)) {
        return std::unexpected(Error::BadRequest);
      }
      host = true;
    } else if (equalsAscii(key, "content-length")) {
      qsizetype size = 0;
      if (result.contentLength || value.empty()) {
        return std::unexpected(Error::BadRequest);
      }
      const auto end = value.data() + value.size();
      const auto parsed = std::from_chars(value.data(), end, size);
      if (parsed.ec != std::errc{} || parsed.ptr != end || size < 0 || size > maximumMediaControlBody ||
          ((result.method || result.eventMethod) && value != "0")) {
        return std::unexpected(Error::BadRequest);
      }
      result.contentLength = size;
    } else if (equalsAscii(key, "soapaction")) {
      if (result.soapAction || value.size() > 128) {
        return std::unexpected(Error::BadRequest);
      }
      result.soapAction = value;
    } else if (equalsAscii(key, "content-type")) {
      if (result.contentType) {
        return std::unexpected(Error::BadRequest);
      }
      result.contentType = value;
    } else if (equalsAscii(key, "user-agent")) {
      if (result.userAgent) {
        return std::unexpected(Error::BadRequest);
      }
      result.userAgent = value;
    } else if (equalsAscii(key, "sid") || equalsAscii(key, "callback") ||
        equalsAscii(key, "nt") || equalsAscii(key, "timeout")) {
      auto& field = equalsAscii(key, "sid") ? result.sid : equalsAscii(key, "callback") ? result.callback :
          equalsAscii(key, "nt") ? result.notificationType : result.timeout;
      if (field || value.empty() || value.size() > 514) {
        return std::unexpected(Error::BadRequest);
      }
      field = value;
    } else if (equalsAscii(key, "transfer-encoding")) {
      return std::unexpected(Error::BadRequest);
    } else if (equalsAscii(key, "if-range") || equalsAscii(key, "if-match") ||
        equalsAscii(key, "if-none-match") || equalsAscii(key, "if-modified-since") ||
        equalsAscii(key, "if-unmodified-since")) {
      return std::unexpected(Error::Unsupported);
    }
    headers.remove_prefix(end + 2);
  }
  if (line.substr(targetEnd + 1) == "HTTP/1.1" && !host) {
    return std::unexpected(Error::BadRequest);
  }
  return result;
}

int hexDigit(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  const auto lower = lowerAscii(value);
  return lower >= 'a' && lower <= 'f' ? lower - 'a' + 10 : -1;
}

std::expected<std::string, Error> decodeResource(std::string_view encoded) {
  std::string decoded;
  decoded.reserve(encoded.size());
  while (!encoded.empty()) {
    auto value = static_cast<unsigned char>(encoded.front());
    if (value == '%') {
      if (encoded.size() < 3 || hexDigit(encoded[1]) < 0 || hexDigit(encoded[2]) < 0) {
        return std::unexpected(Error::BadRequest);
      }
      value = static_cast<unsigned char>(hexDigit(encoded[1]) * 16 + hexDigit(encoded[2]));
      encoded.remove_prefix(2);
    }
    if (value < 32 || value == 127 || value == '/' || value == '\\' || value == '?' || value == '#') {
      return std::unexpected(Error::BadRequest);
    }
    decoded.push_back(static_cast<char>(value));
    encoded.remove_prefix(1);
  }
  return decoded;
}

std::expected<std::pair<std::string, std::size_t>, Error> resourceKey(std::string_view target) {
  constexpr std::string_view prefix = "/Content/";
  if (!target.starts_with(prefix)) {
    return std::unexpected(Error::NotFound);
  }
  target.remove_prefix(prefix.size());
  const auto question = target.find('?');
  const auto resource = decodeResource(target.substr(0, question));
  if (!resource) {
    return std::unexpected(resource.error());
  }
  const std::string_view decoded(*resource);
  const auto key = decoded.substr(0, decoded.find('.'));
  const auto dash = key.find('-');
  const auto hash = key.substr(0, dash);
  if ((dash != 40 && dash != 64) || !std::ranges::all_of(hash, [](char ch) { return hexDigit(ch) >= 0; })) {
    return std::unexpected(Error::NotFound);
  }
  const auto index = key.substr(dash + 1);
  if (index.empty()) {
    return std::unexpected(Error::NotFound);
  }
  std::uint32_t number = 0;
  const auto read = std::from_chars(index.data(), index.data() + index.size(), number);
  if (read.ec != std::errc{} || read.ptr != index.data() + index.size() ||
      number > std::numeric_limits<std::int32_t>::max()) {
    return std::unexpected(Error::NotFound);
  }
  std::string canonical(hash);
  for (auto& ch : canonical) {
    if (ch >= 'a' && ch <= 'f') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }
  return std::pair<std::string, std::size_t>{std::move(canonical), number};
}

std::optional<MediaDeviceDocument> descriptionTarget(std::string_view header) {
  const auto first = header.find(' ');
  if (first == header.npos) {
    return std::nullopt;
  }
  const auto second = header.find(' ', first + 1);
  if (second == header.npos) {
    return std::nullopt;
  }
  const auto target = header.substr(first + 1, second - first - 1);
  if (target == "/RootDevice.xml") { return MediaDeviceDocument::Root; }
  if (target == "/ContentDirectory.xml") { return MediaDeviceDocument::ContentDirectory; }
  if (target == "/ConnectionManager.xml") { return MediaDeviceDocument::ConnectionManager; }
  return std::nullopt;
}

bool downloadRequested(std::string_view target) {
  const auto question = target.find('?');
  if (question == target.npos) {
    return false;
  }
  auto query = target.substr(question + 1);
  while (!query.empty()) {
    const auto next = query.find('&');
    if (query.substr(0, next) == "action=download") {
      return true;
    }
    if (next == query.npos) {
      break;
    }
    query.remove_prefix(next + 1);
  }
  return false;
}

std::optional<std::chrono::seconds> subscriptionLifetime(std::optional<std::string_view> value) {
  using namespace std::chrono_literals;
  if (!value || equalsAscii(*value, "Second-infinite")) {
    return 1800s;
  }
  if (value->size() <= 7 || !equalsAscii(value->substr(0, 7), "Second-")) {
    return std::nullopt;
  }
  const auto raw = value->substr(7);
  std::uint64_t seconds = 0;
  const auto end = raw.data() + raw.size();
  const auto parsed = std::from_chars(raw.data(), end, seconds);
  if (parsed.ec != std::errc{} || parsed.ptr != end || seconds == 0) {
    return std::nullopt;
  }
  return std::chrono::seconds(std::min<std::uint64_t>(seconds, 1800));
}

bool constantEquals(QByteArrayView a, QByteArrayView b) {
  return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), static_cast<std::size_t>(a.size())) == 0;
}

bool authorized(const MediaBasicCredentials& expected, std::optional<std::string_view> header) {
  if (expected.username.isEmpty() || expected.username.size() > 128 || expected.username.contains(':') ||
      expected.passwordSha1.size() != 20 || !header || header->size() > 512 ||
      header->size() <= 6 || !equalsAscii(header->substr(0, 6), "Basic ")) {
    return false;
  }
  const auto decoded = QByteArray::fromBase64Encoding(QByteArray(header->data() + 6,
      static_cast<qsizetype>(header->size() - 6)), QByteArray::AbortOnBase64DecodingErrors);
  const auto colon = decoded.decoded.indexOf(':');
  if (!decoded || colon <= 0 || colon + 1 == decoded.decoded.size()) {
    return false;
  }
  const QByteArrayView pair(decoded.decoded);
  const auto digest = QCryptographicHash::hash(pair.sliced(colon + 1), QCryptographicHash::Sha1);
  const bool nameMatches = constantEquals(pair.first(colon), expected.username);
  const bool digestMatches = constantEquals(digest, expected.passwordSha1);
  return nameMatches && digestMatches;
}
} // namespace

MediaContentRoutes::MediaContentRoutes(std::weak_ptr<engine::PersistentSession> session,
    std::optional<MediaBasicCredentials> credentials)
    : session_(std::move(session)), credentials_(std::move(credentials)) {}

void MediaContentRoutes::configureLan(const MediaLanEndpoint& endpoint, std::uint16_t port) {
  lan_ = endpoint;
  resourceHost_ = endpoint.address().toString().toLatin1();
  lanAuthority_ = resourceHost_ + ':' + QByteArray::number(port);
}

bool MediaContentRoutes::acceptsPeer(const QHostAddress& peer) const {
  return lan_ ? lan_->accepts(peer) : peer.isLoopback();
}

bool MediaContentRoutes::publish(const Selection& file) {
  const auto session = session_.lock();
  if (!session) {
    return false;
  }
  const auto info = session->verifiedFileInfo(file);
  if (!info || info->name.empty() || info->name.size() > 1024) {
    return false;
  }
  const Key key{file.hash.hex(), file.file};
  if (files_.size() + transcodes_.size() >= maximumResources && !files_.contains(key)) {
    return false;
  }
  const auto existing = files_.find(key);
  const QString name = QString::fromStdString(info->name);
  if (existing == files_.end() || existing->second->file.engine != file.engine ||
      existing->second->file.admission != file.admission || existing->second->name != name) {
    if (existing == files_.end() && nextObjectId_ >= std::numeric_limits<std::int32_t>::max() - 1u) {
      return false;
    }
    const auto id = existing == files_.end() ? nextObjectId_ : existing->second->objectId;
    files_.insert_or_assign(key, std::make_shared<const Publication>(Publication{file, id, name, originalMediaType(name)}));
    if (id == nextObjectId_) {
      nextObjectId_ += 2;
    }
    ++updateId_;
  }
  return true;
}

bool MediaContentRoutes::withdraw(const Selection& file) {
  const auto found = files_.find({file.hash.hex(), file.file});
  if (found == files_.end() || found->second->file.engine != file.engine || found->second->file.admission != file.admission) {
    return false;
  }
  files_.erase(found);
  ++updateId_;
  return true;
}

std::optional<MediaDirectoryItem> MediaContentRoutes::Publication::item(const engine::PersistentSession& session) const {
  const auto size = session.verifiedFileSize(file);
  if (!size) { return std::nullopt; }
  if (!artifact) {
    return MediaDirectoryItem{objectId, name, QByteArray::fromStdString(file.hash.hex()), file.file, *size, type, path};
  }
  const auto state = job->state();
  const auto length = artifact->size();
  const bool growing = progressive && state == FfmpegJobState::Running;
  if ((!growing && state != FfmpegJobState::Produced) || length <= 0 ||
      (!progressive && static_cast<std::uint64_t>(length) != bytes)) {
    return std::nullopt;
  }
  return MediaDirectoryItem{objectId, name, QByteArray::fromStdString(file.hash.hex()), file.file,
      growing ? 0 : static_cast<std::uint64_t>(length), type, path, growing};
}

std::optional<MediaRendererResource> MediaContentRoutes::publishTranscode(
    const Selection& file, std::shared_ptr<TorrentTranscode> job, std::uint16_t port) {
  const auto session = session_.lock();
  if (!port || !session || !job) { return std::nullopt; }
  const auto existing = std::ranges::find_if(transcodes_, [&](const auto& entry) { return entry.second->job == job; });
  if (existing != transcodes_.end()) {
    const auto& published = *existing->second;
    if (published.file.engine != file.engine || published.file.hash != file.hash ||
        published.file.admission != file.admission || published.file.file != file.file) {
      return std::nullopt;
    }
    const auto item = published.item(*session);
    return item ? std::optional(rendererResource(*item, port)) : std::nullopt;
  }
  if (files_.size() + transcodes_.size() >= maximumResources ||
      nextObjectId_ >= std::numeric_limits<std::int32_t>::max() - 1u) {
    return std::nullopt;
  }
  auto output = job->openResult(file);
  auto artifact = output ? std::move(output->artifact) : job->openProgressive(file);
  if (!artifact) { return std::nullopt; }
  const bool progressive = !output;
  const auto info = session->verifiedFileInfo(file);
  if (!info || info->name.empty() || info->name.size() > 1024) { return std::nullopt; }
  const auto bytes = artifact->size();
  if (bytes <= 0) { return std::nullopt; }
  QString extension;
  switch (output ? output->format : FfmpegContainer::Mp4) {
    case FfmpegContainer::Matroska: extension = QStringLiteral(".mkv"); break;
    case FfmpegContainer::Mp4: extension = QStringLiteral(".mp4"); break;
    case FfmpegContainer::MpegTs: extension = QStringLiteral(".ts"); break;
  }
  auto name = QString::fromStdString(info->name);
  const auto oldExtension = name.lastIndexOf(u'.');
  if (oldExtension > 0) { name.truncate(oldExtension); }
  name += extension;
  const QByteArray path = "/Content/transcoded-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces) + extension.toLatin1();
  const auto entry = std::make_shared<const Publication>(Publication{file, nextObjectId_, name, originalMediaType(name),
      path, std::move(job), std::move(artifact), static_cast<std::uint64_t>(bytes), progressive});
  const auto item = entry->item(*session);
  if (!item || !transcodes_.emplace(path, entry).second) { return std::nullopt; }
  nextObjectId_ += 2;
  ++updateId_;
  return rendererResource(*item, port);
}

bool MediaContentRoutes::withdrawTranscode(const std::shared_ptr<TorrentTranscode>& job) {
  const auto found = std::ranges::find_if(transcodes_, [&](const auto& entry) { return entry.second->job == job; });
  if (found == transcodes_.end()) { return false; }
  transcodes_.erase(found);
  ++updateId_;
  return true;
}

QByteArray MediaContentRoutes::transcodedUrl(const std::shared_ptr<TorrentTranscode>& job, std::uint16_t port) const {
  const auto session = session_.lock();
  if (!port || !session || !job) { return {}; }
  const auto found = std::ranges::find_if(transcodes_, [&](const auto& entry) { return entry.second->job == job; });
  if (found == transcodes_.end()) { return {}; }
  const auto item = found->second->item(*session);
  return item ? mediaResourceUrl(*item, port, resourceHost_) : QByteArray{};
}

void MediaContentRoutes::clearTranscodes() {
  if (transcodes_.empty()) { return; }
  transcodes_.clear();
  ++updateId_;
}

std::optional<MediaDirectoryItem> MediaContentRoutes::publishedItem(const Selection& file) const {
  const Key key{file.hash.hex(), file.file};
  const auto found = files_.find(key);
  const auto session = session_.lock();
  if (found == files_.end() || !session || found->second->file.engine != file.engine ||
      found->second->file.admission != file.admission) {
    return std::nullopt;
  }
  return found->second->item(*session);
}

QByteArray MediaContentRoutes::publishedUrl(const Selection& file, std::uint16_t port) const {
  const auto item = port ? publishedItem(file) : std::nullopt;
  return item ? mediaResourceUrl(*item, port, resourceHost_) : QByteArray{};
}

std::optional<MediaRendererResource> MediaContentRoutes::rendererResource(const Selection& file, std::uint16_t port) const {
  const auto item = port ? publishedItem(file) : std::nullopt;
  return item ? std::optional(rendererResource(*item, port)) : std::nullopt;
}

MediaRendererResource MediaContentRoutes::rendererResource(const MediaDirectoryItem& item, std::uint16_t port) const {
  auto page = browseMediaDirectory(std::span(&item, 1), port, updateId_, {item.objectId}, resourceHost_);
  QStringList types{QString::fromLatin1(item.type.primary)};
  if (!item.type.alternative.empty()) { types.push_back(QString::fromLatin1(item.type.alternative)); }
  return {mediaResourceUrl(item, port, resourceHost_), std::move(page.didl), std::move(types)};
}

MediaBrowsePage MediaContentRoutes::browse(std::uint16_t port, const MediaBrowseRequest& request) {
  const auto session = session_.lock();
  std::vector<MediaDirectoryItem> items;
  std::vector<std::uint32_t> visible;
  if (session) {
    items.reserve(files_.size() + transcodes_.size());
    visible.reserve(items.capacity());
    const auto append = [&](const auto& publications) {
      for (const auto& [key, entry] : publications) {
        if (auto item = entry->item(*session)) {
          visible.push_back(entry->objectId);
          items.push_back(std::move(*item));
        }
      }
    };
    append(files_);
    append(transcodes_);
  }
  if (visible != lastVisible_) {
    lastVisible_ = std::move(visible);
    ++updateId_;
  }
  return browseMediaDirectory(items, port, updateId_, request, resourceHost_);
}

std::expected<std::optional<MediaDescriptionRequest>, Error> MediaContentRoutes::descriptionRequest(
    const QHostAddress& peer, std::string_view header) const {
  const auto document = descriptionTarget(header);
  if (!document) {
    return std::nullopt;
  }
  if (!acceptsPeer(peer)) {
    return std::unexpected(Error::Forbidden);
  }
  const auto request = parseRequest(header, std::string_view(lanAuthority_));
  if (!request) {
    return std::unexpected(request.error());
  }
  if (credentials_ && !authorized(*credentials_, request->authorization)) {
    return std::unexpected(Error::Unauthorized);
  }
  if (!request->method) {
    return std::unexpected(Error::Unsupported);
  }
  return MediaDescriptionRequest{*document, *request->method == MediaHttpMethod::Get};
}

std::expected<MediaControlRequest, Error> MediaContentRoutes::controlRequest(
    const QHostAddress& peer, std::string_view header) const {
  if (!acceptsPeer(peer)) {
    return std::unexpected(Error::Forbidden);
  }
  const auto request = parseRequest(header, std::string_view(lanAuthority_));
  if (!request) {
    return std::unexpected(request.error());
  }
  if (credentials_ && !authorized(*credentials_, request->authorization)) {
    return std::unexpected(Error::Unauthorized);
  }
  if (request->method || request->eventMethod) {
    return std::unexpected(Error::Unsupported);
  }
  auto service = MediaControlService::ContentDirectory;
  if (request->target == "/ConnectionManager/Control") {
    service = MediaControlService::ConnectionManager;
  } else if (request->target != "/ContentDirectory/Control") {
    return std::unexpected(Error::NotFound);
  }
  const auto type = request->contentType.value_or(std::string_view{});
  if (!request->contentLength || *request->contentLength == 0 || !request->soapAction || request->soapAction->empty() ||
      request->range || !equalsAscii(trim(type.substr(0, type.find(';'))), "text/xml")) {
    return std::unexpected(Error::BadRequest);
  }
  const auto agent = request->userAgent.value_or(std::string_view{});
  const auto action = *request->soapAction;
  return MediaControlRequest{service, *request->contentLength, QByteArray(action.data(), static_cast<qsizetype>(action.size())),
      equalsAscii(agent.substr(0, 4), "xbox")};
}

std::expected<MediaEventRequest, Error> MediaContentRoutes::eventRequest(
    const QHostAddress& peer, std::string_view header) const {
  if (!acceptsPeer(peer)) {
    return std::unexpected(Error::Forbidden);
  }
  const auto request = parseRequest(header, std::string_view(lanAuthority_));
  if (!request) {
    return std::unexpected(request.error());
  }
  if (credentials_ && !authorized(*credentials_, request->authorization)) {
    return std::unexpected(Error::Unauthorized);
  }
  if (!request->eventMethod || request->range || request->soapAction) {
    return std::unexpected(Error::BadRequest);
  }
  auto service = MediaControlService::ContentDirectory;
  if (request->target == "/ConnectionManager/Event") {
    service = MediaControlService::ConnectionManager;
  } else if (request->target != "/ContentDirectory/Event") {
    return std::unexpected(Error::NotFound);
  }
  MediaEventRequest result{service, *request->eventMethod, {}, {}, {}};
  if (request->sid) {
    const auto sid = *request->sid;
    if (sid.size() != 41 || !sid.starts_with("uuid:") || request->callback || request->notificationType) {
      return std::unexpected(Error::BadRequest);
    }
    result.sid = QByteArray(sid.data(), static_cast<qsizetype>(sid.size()));
  }
  if (result.method == MediaEventMethod::Unsubscribe) {
    if (result.sid.isEmpty() || request->timeout) {
      return std::unexpected(Error::BadRequest);
    }
    return result;
  }
  const auto timeout = subscriptionLifetime(request->timeout);
  if (!timeout) {
    return std::unexpected(Error::BadRequest);
  }
  result.timeout = *timeout;
  if (result.sid.isEmpty()) {
    const auto callback = request->callback.value_or(std::string_view{});
    if (!request->notificationType || !equalsAscii(*request->notificationType, "upnp:event") ||
        callback.size() < 3 || callback.front() != '<' || callback.back() != '>') {
      return std::unexpected(Error::BadRequest);
    }
    auto uri = QUrl::fromEncoded(QByteArray(callback.data() + 1, static_cast<qsizetype>(callback.size() - 2)),
        QUrl::StrictMode);
    if (!uri.isValid()) {
      return std::unexpected(Error::BadRequest);
    }
    result.callback = std::move(uri);
  }
  return result;
}

std::expected<std::shared_ptr<const MediaContentRoutes::Publication>, Error> MediaContentRoutes::findPublication(
    std::string_view requestedTarget) const {
  const auto target = requestedTarget.substr(0, requestedTarget.find('?'));
  std::shared_ptr<const Publication> publication;
  if (target.starts_with("/Content/transcoded-")) {
    const auto found = transcodes_.find(QByteArray(target.data(), static_cast<qsizetype>(target.size())));
    if (found != transcodes_.end()) { publication = found->second; }
  } else {
    const auto key = resourceKey(requestedTarget);
    if (!key) { return std::unexpected(key.error()); }
    const auto found = files_.find(*key);
    if (found != files_.end()) { publication = found->second; }
  }
  if (!publication) { return std::unexpected(Error::NotFound); }
  return publication;
}

bool MediaContentRoutes::selectRendererType(const QHostAddress& peer, std::string_view path, QStringView type) {
  if (!acceptsPeer(peer)) { return false; }
  const auto publication = findPublication(path);
  if (!publication) { return false; }
  if (type.isEmpty() || type == QString::fromLatin1((*publication)->type.primary)) {
    clearRendererType();
    return true;
  }
  if (type != QString::fromLatin1((*publication)->type.alternative)) { return false; }
  alternateType_ = RendererTypeBinding{peer, *publication};
  return true;
}

std::expected<std::unique_ptr<TorrentHttpBody>, Error> MediaContentRoutes::resolve(
    const QHostAddress& peer, std::string_view raw) const {
  if (!acceptsPeer(peer)) {
    return std::unexpected(Error::Forbidden);
  }
  const auto request = parseRequest(raw, std::string_view(lanAuthority_));
  if (!request) {
    return std::unexpected(request.error());
  }
  if (credentials_ && !authorized(*credentials_, request->authorization)) {
    return std::unexpected(Error::Unauthorized);
  }
  if (!request->method) {
    return std::unexpected(Error::Unsupported);
  }
  const auto publication = findPublication(request->target);
  if (!publication) { return std::unexpected(publication.error()); }
  const auto& entry = **publication;
  if (entry.progressive && entry.job->state() == FfmpegJobState::Running && !request->http11) {
    return std::unexpected(Error::BadRequest);
  }
  const auto open = [&]() -> std::expected<std::unique_ptr<TorrentHttpBody>, TorrentHttpBody::OpenError> {
    if (!entry.artifact) { return TorrentHttpBody::open(session_, entry.file, *request->method, request->range); }
    if (entry.progressive) {
      const auto session = session_.lock();
      const auto item = session ? entry.item(*session) : std::nullopt;
      if (!item) { return std::unexpected(TorrentHttpBody::OpenError{
          storage::Error{storage::ErrorCode::Conflict, "The live media resource has retired"}}); }
      if (item->growing) {
        return TorrentHttpBody::openProgressive(session_, entry.file, entry.artifact, entry.job,
            *request->method, request->range);
      }
      return TorrentHttpBody::openTranscoded(session_, entry.file, entry.artifact, item->bytes,
          *request->method, request->range);
    }
    return TorrentHttpBody::openTranscoded(session_, entry.file, entry.artifact, entry.bytes,
        *request->method, request->range);
  };
  auto response = open();
  if (!response) {
    return std::unexpected(std::holds_alternative<MediaRangeError>(response.error()) ?
        Error::RangeRejected : Error::NotFound);
  }
  (*response)->publication_ = std::shared_ptr<const Selection>(*publication, &entry.file);
  if (downloadRequested(request->target)) {
    (*response)->attachmentName_ = QUrl::toPercentEncoding(entry.name);
  } else if (entry.type.kind != MediaKind::Unknown) {
    auto& body = **response;
    const bool alternate = alternateType_ && alternateType_->peer == peer &&
        alternateType_->publication.lock() == *publication;
    body.contentType_ = alternate ? entry.type.alternative : entry.type.primary;
    if (!entry.artifact) {
      // Preserve the original renderer hints for the published source class.
      switch (entry.type.kind) {
        case MediaKind::Picture: body.dlnaFeatures_ = "DLNA.ORG_PN=JPEG;"; break;
        case MediaKind::Audio: body.dlnaFeatures_ = "DLNA.ORG_PN=MP3;"; break;
        case MediaKind::Video: body.dlnaFeatures_ = "DLNA.ORG_PN=MPEG_PS_NTSC;"; break;
        case MediaKind::Unknown: break;
      }
    }
    body.dlnaFeatures_ += body.chunked() ? "DLNA.ORG_OP=00;" : "DLNA.ORG_OP=01;";
    body.dlnaFeatures_ += entry.artifact ? "DLNA.ORG_CI=1;" : "DLNA.ORG_CI=0;";
    body.dlnaFeatures_ += "DLNA.ORG_FLAGS=01700000000000000000000000000000";
  }
  return std::move(*response);
}

} // namespace biglybtc::media
