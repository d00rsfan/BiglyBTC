// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_renderer_transport.hpp"

#include <QAbstractEventDispatcher>
#include <QMap>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QThread>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <limits>

namespace biglybtc::media {
namespace {
using namespace std::chrono_literals;
constexpr qsizetype maximumSoap = 32 * 1024;
constexpr qsizetype maximumMetadata = 16 * 1024;
constexpr QLatin1StringView envelopeNamespace("http://schemas.xmlsoap.org/soap/envelope/");
constexpr QLatin1StringView transportNamespace("urn:schemas-upnp-org:service:AVTransport:1");
constexpr QLatin1StringView connectionNamespace("urn:schemas-upnp-org:service:ConnectionManager:1");

bool directEndpoint(const QUrl& url, const QHostAddress& address) {
  return url.isValid() && url.scheme() == QStringLiteral("http") && address.protocol() == QAbstractSocket::IPv4Protocol &&
      url.host() == address.toString() &&
      url.port() > 0 && !url.authority().contains('@') && !url.hasQuery() && !url.hasFragment() &&
      url.path().startsWith(u'/') && url.toEncoded().size() <= 2048;
}

bool timeoutAllowed(std::chrono::milliseconds timeout) {
  return timeout >= 100ms && timeout <= 30s;
}

using Arguments = QMap<QString, QString>;

std::optional<Arguments> expectedResponse(const QByteArray& body, QStringView action, QLatin1StringView service) {
  QXmlStreamReader xml(body);
  xml.setEntityExpansionLimit(1);
  unsigned depth = 0;
  bool completed = false;
  Arguments fields;
  while (!xml.atEnd()) {
    const auto token = xml.readNext();
    if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference ||
        (token == QXmlStreamReader::Characters && !xml.isWhitespace())) {
      return std::nullopt;
    }
    if (token == QXmlStreamReader::StartElement) {
      ++depth;
      if (depth == 4 && service == connectionNamespace &&
          (xml.namespaceUri().isEmpty() || xml.namespaceUri() == service)) {
        const auto field = xml.name().toString();
        if (fields.size() >= 16 || field.size() > 64 || fields.contains(field)) { return std::nullopt; }
        auto value = xml.readElementText().trimmed();
        if (xml.hasError() || value.size() > 8192) { return std::nullopt; }
        fields.insert(field, std::move(value));
        --depth;
        continue;
      }
      const bool valid = !completed &&
          ((depth == 1 && xml.name() == u"Envelope" && xml.namespaceUri() == envelopeNamespace) ||
           (depth == 2 && xml.name() == u"Body" && xml.namespaceUri() == envelopeNamespace) ||
           (depth == 3 && xml.name() == action && xml.namespaceUri() == service));
      if (!valid) {
        return std::nullopt;
      }
    } else if (token == QXmlStreamReader::EndElement) {
      if (depth == 3) {
        completed = true;
      }
      --depth;
    }
  }
  return completed && !xml.hasError() && depth == 0 ? std::optional(std::move(fields)) : std::nullopt;
}

QString preferredType(const QStringList& offers, const QString& sink) {
  const auto protocols = QStringView(sink).split(u',');
  for (const auto& type : offers) {
    for (const auto& protocol : protocols) {
      if (protocol.size() >= 11 && protocol.sliced(11).startsWith(type)) {
        return type;
      }
    }
  }
  return {};
}

std::optional<std::uint32_t> connectionNumber(QStringView text) {
  bool valid = false;
  const auto number = text.toUInt(&valid);
  return valid && number <= std::numeric_limits<std::int32_t>::max() ? std::optional(number) : std::nullopt;
}
} // namespace

MediaRendererTransport::MediaRendererTransport(QUrl control, QObject* parent)
    : MediaRendererTransport(std::move(control), {}, parent) {}

MediaRendererTransport::MediaRendererTransport(QUrl control, QUrl connections, QObject* parent)
    : QObject(parent), endpoint_(std::move(control)), connections_(std::move(connections)), network_(this), deadline_(this) {
  network_.setProxy(QNetworkProxy::NoProxy);
  deadline_.setSingleShot(true);
  connect(&deadline_, &QTimer::timeout, this, [this] {
    if (state_ == State::Completing) { end(afterCompletion_); }
    else { complete(State::Failed); }
  });
}

MediaRendererTransport::MediaRendererTransport(QUrl control, QUrl connections, MediaLanEndpoint endpoint)
    : MediaRendererTransport(std::move(control), std::move(connections)) {
  lan_ = std::move(endpoint);
}

bool MediaRendererTransport::endpointAllowed(const QUrl& url, bool resource) const {
  const QHostAddress address(url.host());
  if (!directEndpoint(url, address)) { return false; }
  if (!lan_) { return address == QHostAddress::LocalHost; }
  return resource ? address == lan_->address() : lan_->accepts(address);
}

MediaRendererTransport::~MediaRendererTransport() {
  end(State::Cancelled);
}

bool MediaRendererTransport::play(const QUrl& resource, QByteArray didl, std::uint32_t instance,
    std::function<bool()> current, std::chrono::milliseconds timeout) {
  if (!begin(resource, std::move(didl), std::move(current), timeout)) { return false; }
  instance_ = instance;
  setUri();
  return true;
}

bool MediaRendererTransport::playNegotiated(const QUrl& resource, QByteArray didl, QStringList types,
    std::function<bool()> current, std::chrono::milliseconds timeout, std::function<bool(QStringView)> selectType) {
  if (!endpointAllowed(connections_, false) || connections_.authority() != endpoint_.authority() ||
      types.isEmpty() || types.size() > 4) {
    return false;
  }
  for (const auto& type : types) {
    if (type.isEmpty() || type.size() > 128 || !type.contains(u'/') || type.contains(u':') || type.contains(u',')) {
      return false;
    }
  }
  if (!begin(resource, std::move(didl), std::move(current), timeout)) { return false; }
  instance_ = 0;
  offered_ = std::move(types);
  selectType_ = std::move(selectType);
  state_ = State::Negotiating;
  send(Action::ProtocolInfo);
  return true;
}

bool MediaRendererTransport::begin(const QUrl& resource, QByteArray didl, std::function<bool()> current,
    std::chrono::milliseconds timeout) {
  if (QThread::currentThread() != thread() || !QAbstractEventDispatcher::instance() ||
      (state_ != State::Idle && state_ != State::Stopped && state_ != State::Cancelled && state_ != State::Failed) ||
      !timeoutAllowed(timeout) || !endpointAllowed(endpoint_, false) || !endpointAllowed(resource, true) ||
      (lan_ && !lan_->available()) ||
      !resource.path().startsWith(u"/Content/") || didl.isEmpty() || didl.size() > maximumMetadata || !current) {
    return false;
  }
  current_ = std::move(current);
  if (!stillCurrent()) {
    current_ = {};
    return false;
  }
  resource_ = resource;
  didl_ = std::move(didl);
  deadline_.start(timeout);
  return true;
}

void MediaRendererTransport::setUri() {
  state_ = State::SettingUri;
  send(Action::SetUri);
  resource_.clear();
  didl_.clear();
  offered_.clear();
  chosen_.clear();
}

bool MediaRendererTransport::stop(std::chrono::milliseconds timeout) {
  if (QThread::currentThread() != thread() || state_ != State::Playing || !timeoutAllowed(timeout)) {
    return false;
  }
  state_ = State::Stopping;
  deadline_.start(timeout);
  send(Action::Stop);
  return true;
}

bool MediaRendererTransport::cancel() {
  if (QThread::currentThread() != thread() || !reply_ || state_ == State::Completing) {
    return false;
  }
  complete(State::Cancelled);
  return true;
}

bool MediaRendererTransport::stillCurrent() const {
  try {
    return current_ && current_();
  } catch (...) {
    return false;
  }
}

QLatin1StringView MediaRendererTransport::actionName(Action action) {
  switch (action) {
    case Action::ProtocolInfo: return QLatin1StringView("GetProtocolInfo");
    case Action::Prepare: return QLatin1StringView("PrepareForConnection");
    case Action::CurrentConnections: return QLatin1StringView("GetCurrentConnectionIDs");
    case Action::SetUri: return QLatin1StringView("SetAVTransportURI");
    case Action::Play: return QLatin1StringView("Play");
    case Action::Stop: return QLatin1StringView("Stop");
    case Action::Complete: return QLatin1StringView("ConnectionComplete");
  }
  return {};
}

bool MediaRendererTransport::connectionAction(Action action) {
  return action == Action::ProtocolInfo || action == Action::Prepare || action == Action::CurrentConnections ||
      action == Action::Complete;
}

void MediaRendererTransport::send(Action action) {
  if (lan_ && !lan_->available()) {
    end(action == Action::Complete ? afterCompletion_ : State::Failed);
    return;
  }
  const auto name = actionName(action).toString();
  const bool manager = connectionAction(action);
  const auto service = manager ? connectionNamespace : transportNamespace;
  QByteArray soap;
  QXmlStreamWriter xml(&soap);
  xml.writeStartDocument();
  xml.writeStartElement("s:Envelope");
  xml.writeNamespace(envelopeNamespace.toString(), "s");
  xml.writeStartElement("s:Body");
  xml.writeStartElement(QStringLiteral("u:") + name);
  xml.writeNamespace(service.toString(), "u");
  if (!manager) { xml.writeTextElement("InstanceID", QString::number(instance_)); }
  if (action == Action::SetUri) {
    xml.writeTextElement("CurrentURI", QString::fromLatin1(resource_.toEncoded()));
    xml.writeTextElement("CurrentURIMetaData", QString::fromUtf8(didl_));
  } else if (action == Action::Play) {
    xml.writeTextElement("Speed", "1");
  } else if (action == Action::Prepare) {
    xml.writeTextElement("RemoteProtocolInfo", QStringLiteral("http-get:*:") + chosen_ + QStringLiteral(":*"));
    xml.writeTextElement("PeerConnectionManager", QString{});
    xml.writeTextElement("PeerConnectionID", "-1");
    xml.writeTextElement("Direction", "Input");
  } else if (action == Action::Complete) {
    xml.writeTextElement("ConnectionID", QString::number(*connection_));
  }
  xml.writeEndDocument();
  if (xml.hasError()) {
    end(action == Action::Complete ? afterCompletion_ : State::Failed);
    return;
  }
  QNetworkRequest request(manager ? connections_ : endpoint_);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=UTF-8"));
  request.setRawHeader("SOAPAction", '"' + service.toString().toLatin1() + '#' + name.toLatin1() + '"');
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  request.setRawHeader("Connection", "close");
  input_.clear();
  auto* reply = network_.post(request, soap);
  reply_ = reply;
  reply->setReadBufferSize(maximumSoap + 1);
  connect(reply, &QNetworkReply::readyRead, this, [this, action] {
    if (!collect()) {
      if (action == Action::Complete) { end(afterCompletion_); }
      else { complete(State::Failed); }
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, action] { finished(reply, action); });
}

bool MediaRendererTransport::collect() {
  if (reply_ && reply_->bytesAvailable() > 0) {
    input_ += reply_->read(maximumSoap + 1 - input_.size());
  }
  return input_.size() <= maximumSoap;
}

void MediaRendererTransport::finished(QNetworkReply* reply, Action action) {
  const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toUInt();
  const auto name = actionName(action).toString() + QStringLiteral("Response");
  const auto response = collect() && status == 200 && reply->error() == QNetworkReply::NoError
      ? expectedResponse(input_, name, connectionAction(action) ? connectionNamespace : transportNamespace)
      : std::nullopt;
  reply_ = nullptr;
  reply->deleteLater();
  if (action == Action::Complete) {
    end(afterCompletion_);
    return;
  }
  if (action == Action::Stop) {
    complete(response ? State::Stopped : State::Failed);
    return;
  }
  if (action == Action::Prepare && response) {
    connection_ = connectionNumber(response->value(QStringLiteral("ConnectionID")));
    prepared_ = connection_.has_value();
    instance_ = connectionNumber(response->value(QStringLiteral("AVTransportID"))).value_or(0);
  }
  if (!stillCurrent()) {
    complete(State::Failed);
    return;
  }
  switch (action) {
    case Action::ProtocolInfo:
      chosen_ = response ? preferredType(offered_, response->value(QStringLiteral("Sink"))) : QString{};
      if (selectType_) {
        const auto select = std::move(selectType_);
        try {
          if (!select(chosen_)) { complete(State::Failed); break; }
        } catch (...) {
          complete(State::Failed);
          break;
        }
      }
      send(chosen_.isEmpty() ? Action::CurrentConnections : Action::Prepare);
      break;
    case Action::Prepare:
      if (prepared_) { setUri(); }
      else { send(Action::CurrentConnections); }
      break;
    case Action::CurrentConnections:
      if (response) {
        const auto ids = response->value(QStringLiteral("ConnectionIDs"));
        const auto first = QStringView(ids).left(ids.indexOf(u',') < 0 ? ids.size() : ids.indexOf(u','));
        connection_ = connectionNumber(first);
      }
      instance_ = 0;
      setUri();
      break;
    case Action::SetUri:
      if (response) {
        state_ = State::Starting;
        send(Action::Play);
      } else {
        complete(State::Failed);
      }
      break;
    case Action::Play:
      if (response) { end(State::Playing); }
      else { complete(State::Failed); }
      break;
    case Action::Stop:
    case Action::Complete:
      break;
  }
}

void MediaRendererTransport::releaseReply() {
  if (reply_) {
    auto* reply = reply_.data();
    reply_ = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
  }
}

void MediaRendererTransport::complete(State terminal) {
  if (!prepared_) { end(terminal); return; }
  releaseReply();
  state_ = State::Completing;
  afterCompletion_ = terminal;
  deadline_.start(1s);
  send(Action::Complete);
}

void MediaRendererTransport::end(State terminal) {
  state_ = terminal;
  deadline_.stop();
  releaseReply();
  current_ = {};
  selectType_ = {};
  input_.clear();
  resource_.clear();
  didl_.clear();
  offered_.clear();
  chosen_.clear();
  if (terminal != State::Playing) {
    connection_.reset();
    prepared_ = false;
  }
}

} // namespace biglybtc::media
