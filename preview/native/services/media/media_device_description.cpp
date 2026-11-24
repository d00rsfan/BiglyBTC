// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_device_description.hpp"

#include <QXmlStreamWriter>
#include <algorithm>
#include <span>

namespace biglybtc::media {
namespace {
struct Variable {
  const char* name;
  const char* type;
  bool events = false;
};

struct Argument {
  const char* name;
  const char* variable;
  bool input = false;
};

struct Action {
  const char* name;
  std::span<const Argument> arguments;
};

constexpr Variable directoryVariables[]{
  {"SearchCapabilities", "string"}, {"SortCapabilities", "string"}, {"SystemUpdateID", "ui4", true},
  {"A_ARG_TYPE_ObjectID", "string"}, {"A_ARG_TYPE_BrowseFlag", "string"}, {"A_ARG_TYPE_Filter", "string"},
  {"A_ARG_TYPE_Index", "ui4"}, {"A_ARG_TYPE_Count", "ui4"}, {"A_ARG_TYPE_SortCriteria", "string"},
  {"A_ARG_TYPE_Result", "string"}, {"A_ARG_TYPE_UpdateID", "ui4"},
};
constexpr Argument browse[]{
  {"ObjectID", "A_ARG_TYPE_ObjectID", true}, {"BrowseFlag", "A_ARG_TYPE_BrowseFlag", true},
  {"Filter", "A_ARG_TYPE_Filter", true}, {"StartingIndex", "A_ARG_TYPE_Index", true},
  {"RequestedCount", "A_ARG_TYPE_Count", true}, {"SortCriteria", "A_ARG_TYPE_SortCriteria", true},
  {"Result", "A_ARG_TYPE_Result"}, {"NumberReturned", "A_ARG_TYPE_Count"},
  {"TotalMatches", "A_ARG_TYPE_Count"}, {"UpdateID", "A_ARG_TYPE_UpdateID"},
};
constexpr Argument searchCapabilities[]{{"SearchCaps", "SearchCapabilities"}};
constexpr Argument sortCapabilities[]{{"SortCaps", "SortCapabilities"}};
constexpr Argument systemUpdate[]{{"Id", "SystemUpdateID"}};
constexpr Action directoryActions[]{
  {"GetSearchCapabilities", searchCapabilities}, {"GetSortCapabilities", sortCapabilities},
  {"GetSystemUpdateID", systemUpdate}, {"Browse", browse},
};

constexpr Variable connectionVariables[]{
  {"SourceProtocolInfo", "string", true}, {"SinkProtocolInfo", "string", true}, {"CurrentConnectionIDs", "string", true},
  {"A_ARG_TYPE_ConnectionID", "i4"}, {"A_ARG_TYPE_RcsID", "i4"}, {"A_ARG_TYPE_AVTransportID", "i4"},
  {"A_ARG_TYPE_ProtocolInfo", "string"}, {"A_ARG_TYPE_ConnectionManager", "string"},
  {"A_ARG_TYPE_Direction", "string"}, {"A_ARG_TYPE_ConnectionStatus", "string"},
};
constexpr Argument protocolInfo[]{{"Source", "SourceProtocolInfo"}, {"Sink", "SinkProtocolInfo"}};
constexpr Argument currentIds[]{{"ConnectionIDs", "CurrentConnectionIDs"}};
constexpr Argument connectionInfo[]{
  {"ConnectionID", "A_ARG_TYPE_ConnectionID", true}, {"RcsID", "A_ARG_TYPE_RcsID"},
  {"AVTransportID", "A_ARG_TYPE_AVTransportID"}, {"ProtocolInfo", "A_ARG_TYPE_ProtocolInfo"},
  {"PeerConnectionManager", "A_ARG_TYPE_ConnectionManager"}, {"PeerConnectionID", "A_ARG_TYPE_ConnectionID"},
  {"Direction", "A_ARG_TYPE_Direction"}, {"Status", "A_ARG_TYPE_ConnectionStatus"},
};
constexpr Action connectionActions[]{
  {"GetCurrentConnectionIDs", currentIds}, {"GetCurrentConnectionInfo", connectionInfo},
  {"GetProtocolInfo", protocolInfo},
};

void specVersion(QXmlStreamWriter& xml) {
  xml.writeStartElement("specVersion");
  xml.writeTextElement("major", "1");
  xml.writeTextElement("minor", "0");
  xml.writeEndElement();
}

void service(QXmlStreamWriter& xml, const QString& name) {
  xml.writeStartElement("service");
  xml.writeTextElement("serviceType", QStringLiteral("urn:schemas-upnp-org:service:") + name + ":1");
  xml.writeTextElement("serviceId", QStringLiteral("urn:upnp-org:serviceId:") + name);
  xml.writeTextElement("SCPDURL", '/' + name + ".xml");
  xml.writeTextElement("controlURL", '/' + name + "/Control");
  xml.writeTextElement("eventSubURL", '/' + name + "/Event");
  xml.writeEndElement();
}

void root(QXmlStreamWriter& xml, const MediaDeviceIdentity& identity) {
  xml.writeStartElement("root");
  xml.writeDefaultNamespace("urn:schemas-upnp-org:device-1-0");
  specVersion(xml);
  xml.writeStartElement("device");
  xml.writeTextElement("deviceType", "urn:schemas-upnp-org:device:MediaServer:1");
  xml.writeTextElement("friendlyName", identity.name);
  xml.writeTextElement("manufacturer", "BiglyBT");
  xml.writeTextElement("modelDescription", "Torrent media content server");
  xml.writeTextElement("modelName", "BiglyBT Media Server");
  xml.writeTextElement("UDN", identity.udn());
  xml.writeStartElement("serviceList");
  service(xml, QStringLiteral("ConnectionManager"));
  service(xml, QStringLiteral("ContentDirectory"));
}

void scpd(QXmlStreamWriter& xml, std::span<const Action> actions, std::span<const Variable> variables) {
  xml.writeStartElement("scpd");
  xml.writeDefaultNamespace("urn:schemas-upnp-org:service-1-0");
  specVersion(xml);
  xml.writeStartElement("actionList");
  for (const auto& action : actions) {
    xml.writeStartElement("action");
    xml.writeTextElement("name", QString::fromLatin1(action.name));
    xml.writeStartElement("argumentList");
    for (const auto& argument : action.arguments) {
      xml.writeStartElement("argument");
      xml.writeTextElement("name", QString::fromLatin1(argument.name));
      xml.writeTextElement("direction", argument.input ? "in" : "out");
      xml.writeTextElement("relatedStateVariable", QString::fromLatin1(argument.variable));
      xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();
  }
  xml.writeEndElement();
  xml.writeStartElement("serviceStateTable");
  for (const auto& variable : variables) {
    xml.writeStartElement("stateVariable");
    xml.writeAttribute("sendEvents", variable.events ? "yes" : "no");
    xml.writeTextElement("name", QString::fromLatin1(variable.name));
    xml.writeTextElement("dataType", QString::fromLatin1(variable.type));
    xml.writeEndElement();
  }
}
} // namespace

bool MediaDeviceIdentity::valid() const {
  return !id.isNull() && name.size() <= 128 && !name.trimmed().isEmpty() && name.isValidUtf16() &&
      std::ranges::all_of(name, [](QChar c) { return c.unicode() >= 32 && c.unicode() < 0xfffe; });
}

QString MediaDeviceIdentity::udn() const {
  return QStringLiteral("uuid:") + id.toString(QUuid::WithoutBraces);
}

QByteArray describeMediaDevice(const MediaDeviceIdentity& identity, MediaDeviceDocument requested) {
  if (!identity.valid()) {
    return {};
  }
  QByteArray result;
  QXmlStreamWriter xml(&result);
  xml.writeStartDocument();
  switch (requested) {
  case MediaDeviceDocument::Root:
    root(xml, identity);
    break;
  case MediaDeviceDocument::ContentDirectory:
    scpd(xml, directoryActions, directoryVariables);
    break;
  case MediaDeviceDocument::ConnectionManager:
    scpd(xml, connectionActions, connectionVariables);
    break;
  }
  xml.writeEndDocument();
  return xml.hasError() ? QByteArray{} : result;
}

} // namespace biglybtc::media
