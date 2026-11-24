// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_directory_control.hpp"
#include "media_content_server.hpp"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <charconv>
#include <limits>
#include <map>

namespace biglybtc::media {
namespace {
constexpr QLatin1StringView soapNamespace("http://schemas.xmlsoap.org/soap/envelope/");
constexpr QLatin1StringView directoryNamespace("urn:schemas-upnp-org:service:ContentDirectory:1");
constexpr QLatin1StringView connectionNamespace("urn:schemas-upnp-org:service:ConnectionManager:1");
constexpr QLatin1StringView faultNamespace("urn:schemas-upnp-org:control-1-0");
enum class Action {
  Browse, SystemUpdate, SearchCapabilities, SortCapabilities, ProtocolInfo, ConnectionIds, ConnectionInfo
};
enum class Fault { InvalidAction = 401, InvalidArguments = 402, Failed = 501, BadSort = 709 };

std::optional<Action> identify(MediaControlService service, QByteArrayView action) {
  const auto actionPrefix = service == MediaControlService::ContentDirectory ?
      QByteArrayView("urn:schemas-upnp-org:service:ContentDirectory:1#") :
      QByteArrayView("urn:schemas-upnp-org:service:ConnectionManager:1#");
  if (action.size() >= 2 && action.front() == '"' && action.back() == '"') {
    action = action.sliced(1, action.size() - 2);
  }
  if (!action.startsWith(actionPrefix)) {
    return std::nullopt;
  }
  action = action.sliced(actionPrefix.size());
  if (service == MediaControlService::ConnectionManager) {
    if (action == "GetProtocolInfo") { return Action::ProtocolInfo; }
    if (action == "GetCurrentConnectionIDs") { return Action::ConnectionIds; }
    if (action == "GetCurrentConnectionInfo") { return Action::ConnectionInfo; }
    return std::nullopt;
  }
  if (action == "Browse") { return Action::Browse; }
  if (action == "GetSystemUpdateID") { return Action::SystemUpdate; }
  if (action == "GetSearchCapabilities") { return Action::SearchCapabilities; }
  if (action == "GetSortCapabilities") { return Action::SortCapabilities; }
  return std::nullopt;
}

QString name(Action action) {
  switch (action) {
  case Action::Browse: return QStringLiteral("Browse");
  case Action::SystemUpdate: return QStringLiteral("GetSystemUpdateID");
  case Action::SearchCapabilities: return QStringLiteral("GetSearchCapabilities");
  case Action::SortCapabilities: return QStringLiteral("GetSortCapabilities");
  case Action::ProtocolInfo: return QStringLiteral("GetProtocolInfo");
  case Action::ConnectionIds: return QStringLiteral("GetCurrentConnectionIDs");
  case Action::ConnectionInfo: return QStringLiteral("GetCurrentConnectionInfo");
  }
  return {};
}

QXmlStreamReader::TokenType markup(QXmlStreamReader& xml) {
  while (!xml.atEnd()) {
    const auto token = xml.readNext();
    if (token == QXmlStreamReader::StartDocument || token == QXmlStreamReader::Comment ||
        (token == QXmlStreamReader::Characters && xml.isWhitespace())) {
      continue;
    }
    return token;
  }
  return xml.tokenType();
}

bool element(const QXmlStreamReader& xml, QXmlStreamReader::TokenType kind, QStringView tag,
    QLatin1StringView xmlNamespace) {
  return xml.tokenType() == kind && xml.name() == tag && xml.namespaceUri() == xmlNamespace;
}

using Arguments = std::map<QString, QString>;
std::expected<Arguments, Fault> parseArguments(Action action, QByteArrayView body,
    QLatin1StringView serviceNamespace) {
  if (body.isEmpty() || body.size() > maximumMediaControlBody) {
    return std::unexpected(Fault::InvalidArguments);
  }
  QXmlStreamReader xml(body);
  xml.setEntityExpansionLimit(0);
  using Xml = QXmlStreamReader;
  if (markup(xml) != Xml::StartElement || !element(xml, Xml::StartElement, u"Envelope", soapNamespace) ||
      markup(xml) != Xml::StartElement || !element(xml, Xml::StartElement, u"Body", soapNamespace)) {
    return std::unexpected(Fault::InvalidArguments);
  }
  if (markup(xml) != Xml::StartElement || !element(xml, Xml::StartElement, name(action), serviceNamespace)) {
    return std::unexpected(Fault::InvalidAction);
  }
  Arguments returned;
  auto token = markup(xml);
  while (token == Xml::StartElement) {
    if ((action != Action::Browse && action != Action::ConnectionInfo) || returned.size() == 6 ||
        (!xml.namespaceUri().isEmpty() && xml.namespaceUri() != serviceNamespace)) {
      return std::unexpected(Fault::InvalidArguments);
    }
    const auto key = xml.name().toString();
    auto value = xml.readElementText();
    if (xml.hasError() || key.size() > 32 || value.size() > 1024 ||
        !returned.try_emplace(key, std::move(value)).second) {
      return std::unexpected(Fault::InvalidArguments);
    }
    token = markup(xml);
  }
  if (!element(xml, Xml::EndElement, name(action), serviceNamespace) ||
      markup(xml) != Xml::EndElement || !element(xml, Xml::EndElement, u"Body", soapNamespace) ||
      markup(xml) != Xml::EndElement || !element(xml, Xml::EndElement, u"Envelope", soapNamespace) ||
      markup(xml) != Xml::EndDocument || xml.hasError()) {
    return std::unexpected(Fault::InvalidArguments);
  }
  return returned;
}

std::optional<std::uint32_t> number(const Arguments& arguments, const QString& key) {
  const auto found = arguments.find(key);
  if (found == arguments.end()) {
    return std::nullopt;
  }
  const auto bytes = found->second.toLatin1();
  std::uint32_t parsed = 0;
  const auto last = bytes.constData() + bytes.size();
  const auto result = std::from_chars(bytes.constData(), last, parsed);
  return result.ec == std::errc{} && result.ptr == last ? std::optional{parsed} : std::nullopt;
}

bool connectionIdValid(const Arguments& arguments) {
  const auto found = arguments.find(QStringLiteral("ConnectionID"));
  if (arguments.size() != 1 || found == arguments.end()) {
    return false;
  }
  const auto value = found->second.toLatin1();
  std::int32_t id = 0;
  const auto end = value.constData() + value.size();
  const auto parsed = std::from_chars(value.constData(), end, id);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::expected<MediaBrowseRequest, Fault> browseRequest(const Arguments& args, bool xbox) {
  constexpr const char* keys[]{"ObjectID", "BrowseFlag", "Filter", "StartingIndex", "RequestedCount", "SortCriteria"};
  if (!std::ranges::all_of(keys, [&](const char* key) { return args.contains(QString::fromLatin1(key)); })) {
    return std::unexpected(Fault::InvalidArguments);
  }
  const auto id = number(args, QStringLiteral("ObjectID"));
  const auto first = number(args, QStringLiteral("StartingIndex"));
  const auto count = number(args, QStringLiteral("RequestedCount"));
  const auto& flag = args.at(QStringLiteral("BrowseFlag"));
  if (!id || !first || !count || *id > std::numeric_limits<std::int32_t>::max() ||
      (flag != "BrowseMetadata" && flag != "BrowseDirectChildren")) {
    return std::unexpected(Fault::InvalidArguments);
  }
  const auto& sort = args.at(QStringLiteral("SortCriteria"));
  if (!sort.isEmpty() && sort != "+dc:title") {
    return std::unexpected(Fault::BadSort);
  }
  return MediaBrowseRequest{*id, flag == "BrowseMetadata" ? MediaBrowseMode::Metadata : MediaBrowseMode::DirectChildren,
      *first, *count, xbox};
}

void envelope(QXmlStreamWriter& xml) {
  xml.writeStartDocument();
  xml.writeStartElement("s:Envelope");
  xml.writeNamespace(soapNamespace.toString(), "s");
  xml.writeStartElement("s:Body");
}

QString faultText(Fault fault) {
  switch (fault) {
  case Fault::InvalidAction: return QStringLiteral("Invalid Action");
  case Fault::InvalidArguments: return QStringLiteral("Invalid Args");
  case Fault::Failed: return QStringLiteral("Action Failed");
  case Fault::BadSort: return QStringLiteral("Unsupported sort criteria");
  }
  return {};
}

MediaControlReply fail(Fault reason) {
  MediaControlReply result{500, {}};
  QXmlStreamWriter xml(&result.xml);
  envelope(xml);
  xml.writeStartElement("s:Fault");
  xml.writeTextElement("faultcode", reason == Fault::Failed ? "s:Server" : "s:Client");
  xml.writeTextElement("faultstring", "UPnPError");
  xml.writeStartElement("detail");
  xml.writeStartElement("UPnPError");
  xml.writeDefaultNamespace(faultNamespace.toString());
  xml.writeTextElement("errorCode", QString::number(static_cast<int>(reason)));
  xml.writeTextElement("errorDescription", faultText(reason));
  xml.writeEndDocument();
  return result;
}
} // namespace

MediaControlReply controlMediaDevice(MediaContentServer& server, MediaControlService service,
    QByteArrayView header, QByteArrayView body, bool xbox) {
  const auto action = identify(service, header);
  if (!action) {
    return fail(Fault::InvalidAction);
  }
  const auto serviceNamespace = service == MediaControlService::ContentDirectory ?
      directoryNamespace : connectionNamespace;
  const auto args = parseArguments(*action, body, serviceNamespace);
  if (!args) {
    return fail(args.error());
  }
  if (*action == Action::ConnectionInfo && !connectionIdValid(*args)) {
    return fail(Fault::InvalidArguments);
  }
  MediaBrowseRequest request;
  if (*action == Action::Browse) {
    const auto parsed = browseRequest(*args, xbox);
    if (!parsed) {
      return fail(parsed.error());
    }
    request = *parsed;
  }
  std::optional<MediaBrowsePage> page;
  if (*action == Action::Browse || *action == Action::SystemUpdate) {
    page = server.browse(request);
    if (!page) {
      return fail(Fault::Failed);
    }
  }
  MediaControlReply result{200, {}};
  QXmlStreamWriter xml(&result.xml);
  envelope(xml);
  xml.writeStartElement(QStringLiteral("u:") + name(*action) + QStringLiteral("Response"));
  xml.writeNamespace(serviceNamespace.toString(), "u");
  switch (*action) {
  case Action::Browse:
    xml.writeTextElement("Result", QString::fromUtf8(page->didl));
    xml.writeTextElement("NumberReturned", QString::number(page->numberReturned));
    xml.writeTextElement("TotalMatches", QString::number(page->totalMatches));
    xml.writeTextElement("UpdateID", QString::number(page->updateId));
    break;
  case Action::SystemUpdate:
    xml.writeTextElement("Id", QString::number(page->updateId));
    break;
  case Action::SearchCapabilities:
    xml.writeTextElement("SearchCaps", "");
    break;
  case Action::SortCapabilities:
    xml.writeTextElement("SortCaps", "dc:title");
    break;
  case Action::ProtocolInfo:
    xml.writeTextElement("Source", QString::fromLatin1(mediaSourceProtocols()));
    xml.writeTextElement("Sink", "");
    break;
  case Action::ConnectionIds:
    xml.writeTextElement("ConnectionIDs", "");
    break;
  case Action::ConnectionInfo:
    xml.writeTextElement("RcsID", "0");
    xml.writeTextElement("AVTransportID", "0");
    xml.writeTextElement("ProtocolInfo", "");
    xml.writeTextElement("PeerConnectionManager", "");
    xml.writeTextElement("PeerConnectionID", "-1");
    xml.writeTextElement("Direction", "Input");
    xml.writeTextElement("Status", "Unknown");
    break;
  }
  xml.writeEndDocument();
  return result;
}

} // namespace biglybtc::media
