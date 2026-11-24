// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_release_transport.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QSslSocket>
#include <QTimer>
#include <algorithm>

namespace biglybtc::update {
namespace {
constexpr qint64 maximumAsset = 65 * 1024 * 1024;
constexpr int maximumRedirects = 3;
using Error = PluginDownloadError;

std::optional<QNetworkProxy> selectedProxy(const PluginUpdatePlan& plan) {
  if (plan.route == UpdateNetworkRoute::Direct && !plan.proxy) {
    return QNetworkProxy(QNetworkProxy::NoProxy);
  }
  if (plan.route != UpdateNetworkRoute::WebProxy || !plan.proxy) { return std::nullopt; }
  const auto& endpoint = *plan.proxy;
  if (endpoint.host.empty() || endpoint.port <= 0 || endpoint.port > 65535 || !endpoint.remoteDns) { return std::nullopt; }
  QNetworkProxy::ProxyType type;
  if (endpoint.kind == engine::NetworkProxyKind::Http) { type = QNetworkProxy::HttpProxy; }
  else if (endpoint.kind == engine::NetworkProxyKind::Socks5) { type = QNetworkProxy::Socks5Proxy; }
  else { return std::nullopt; }
  return QNetworkProxy(type, QString::fromStdString(endpoint.host), static_cast<quint16>(endpoint.port),
      QString::fromStdString(endpoint.username), QString::fromStdString(endpoint.password));
}

bool permittedUrl(const QUrl& url, const QString& origin, const QStringList& deliveryHosts) {
  return url.isValid() && !url.isRelative() && url.scheme() == u"https" && url.port() == -1 &&
      !url.authority().contains('@') && !url.hasFragment() &&
      (url.host() == origin || deliveryHosts.contains(url.host()));
}

bool isRedirect(int code) {
  return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
}

struct Reply {
  int status;
  QUrl redirect;
  QByteArray contents;
};

std::expected<Reply, Error> fetch(QNetworkAccessManager& manager, const QUrl& url, qint64 byteLimit,
    const PluginDownloadOptions& options, const QDeadlineTimer& limit, Error sizeError) {
  if (options.cancellation.stop_requested()) { return std::unexpected(Error::Cancelled); }
  if (limit.hasExpired()) { return std::unexpected(Error::Deadline); }
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  request.setRawHeader("Accept-Encoding", "identity");
  auto tls = options.tls;
  tls.setProtocol(QSsl::TlsV1_2OrLater);
  request.setSslConfiguration(tls);
  auto* response = manager.get(request);
  if (!response) { return std::unexpected(Error::Response); }
  auto dispose = qScopeGuard([&] { response->deleteLater(); });
  response->setReadBufferSize(64 * 1024);

  QEventLoop loop;
  QTimer timer, cancellation;
  timer.setSingleShot(true);
  bool expired = false;
  bool tooLarge = false;
  QByteArray body;
  const auto read = [&] {
    while (response->bytesAvailable() > 0) {
      const auto amount = std::min<qint64>(response->bytesAvailable(), byteLimit - body.size() + 1);
      const auto data = response->read(amount);
      if (data.size() > byteLimit - body.size()) {
        tooLarge = true;
        response->abort();
        return;
      }
      if (data.isEmpty()) { return; }
      body += data;
    }
  };
  QObject::connect(response, &QNetworkReply::readyRead, &loop, read);
  QObject::connect(response, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
    expired = true;
    response->abort();
    loop.quit();
  });
  if (options.cancellation.stop_possible()) {
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
      if (options.cancellation.stop_requested()) {
        response->abort();
        loop.quit();
      }
    });
  }
  if (!response->isFinished()) {
    if (options.cancellation.stop_possible()) { cancellation.start(25); }
    timer.start(static_cast<int>(std::max<qint64>(1, limit.remainingTime())));
    loop.exec(QEventLoop::ExcludeUserInputEvents);
  }
  if (options.cancellation.stop_requested()) { return std::unexpected(Error::Cancelled); }
  read();
  if (expired || limit.hasExpired()) { return std::unexpected(Error::Deadline); }
  if (tooLarge) { return std::unexpected(sizeError); }
  if (response->error() == QNetworkReply::SslHandshakeFailedError) { return std::unexpected(Error::Tls); }
  if (response->error() != QNetworkReply::NoError) { return std::unexpected(Error::Response); }
  const auto status = response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto redirect = response->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
  return Reply{status, redirect, std::move(body)};
}
std::optional<QNetworkProxy> admittedProxy(const PluginUpdatePlan& plan, const PluginDownloadOptions& options) {
  const auto mode = options.tls.peerVerifyMode();
  if (options.deadline.count() <= 0 || options.deadline > std::chrono::minutes(2) ||
      (mode != QSslSocket::AutoVerifyPeer && mode != QSslSocket::VerifyPeer)) {
    return std::nullopt;
  }
  return selectedProxy(plan);
}

std::expected<QByteArray, Error> download(QNetworkAccessManager& manager, QUrl url, qint64 byteLimit,
    const PluginDownloadOptions& options, const QDeadlineTimer& limit, Error sizeError, QStringView originPath = {}) {
  const auto origin = url.host();
  if (!permittedUrl(url, origin, options.deliveryHosts)) { return std::unexpected(Error::Policy); }
  for (int count = 0; count <= maximumRedirects; ++count) {
    auto reply = fetch(manager, url, byteLimit, options, limit, sizeError);
    if (!reply) { return std::unexpected(reply.error()); }
    if (isRedirect(reply->status)) {
      if (count == maximumRedirects || reply->redirect.isEmpty()) { return std::unexpected(Error::Redirect); }
      url = url.resolved(reply->redirect);
      if (!permittedUrl(url, origin, options.deliveryHosts) ||
          (url.host() == origin && !url.path(QUrl::FullyEncoded).startsWith(originPath))) {
        return std::unexpected(Error::Redirect);
      }
      continue;
    }
    if (reply->status != 200) { return std::unexpected(Error::Response); }
    return std::move(reply->contents);
  }
  return std::unexpected(Error::Redirect);
}
} // namespace

std::expected<VerifiedPluginRelease, PluginCatalogueCheckError> checkPluginReleaseCatalogue(
    const PluginReleaseTarget& target, QStringView name, QByteArrayView signingKey, qint64 lastSequence,
    const PluginUpdatePlan& plan, const PluginDownloadOptions& options) {
  if (options.cancellation.stop_requested()) { return std::unexpected(PluginCatalogueCheckError{Error::Cancelled}); }
  const auto url = pluginReleaseCatalogueUrl(target, name);
  if (!url || signingKey.size() != 32 || lastSequence < 0) {
    return std::unexpected(PluginCatalogueCheckError{CatalogueError::InvalidInput});
  }
  const auto proxy = admittedProxy(plan, options);
  if (!proxy) { return std::unexpected(PluginCatalogueCheckError{Error::Policy}); }
  const QDeadlineTimer limit(options.deadline.count());
  QNetworkAccessManager manager;
  manager.setProxy(*proxy);

  const auto repositoryPath = QStringLiteral("/") + target.repository + QStringLiteral("/releases/");
  auto body = download(manager, *url, MaximumReleaseCatalogueBytes, options, limit, Error::CatalogueSize, repositoryPath);
  if (!body) { return std::unexpected(PluginCatalogueCheckError{body.error()}); }
  if (body->isEmpty()) { return std::unexpected(PluginCatalogueCheckError{CatalogueError::InvalidInput}); }
  auto signatureUrl = *url;
  signatureUrl.setPath(url->path() + QStringLiteral(".sig"));
  auto signature = download(manager, std::move(signatureUrl), 64, options, limit, Error::CatalogueSize, repositoryPath);
  if (!signature) { return std::unexpected(PluginCatalogueCheckError{signature.error()}); }

  auto release = verifyPluginReleaseCatalogue(*body, *signature, signingKey, target,
      lastSequence, QDateTime::currentSecsSinceEpoch());
  if (options.cancellation.stop_requested()) { return std::unexpected(PluginCatalogueCheckError{Error::Cancelled}); }
  if (!release) { return std::unexpected(PluginCatalogueCheckError{release.error()}); }
  return std::move(*release);
}

std::expected<QByteArray, Error> downloadPluginReleaseAsset(
    const VerifiedPluginRelease& release, const PluginUpdatePlan& plan, const PluginDownloadOptions& options) {
  if (options.cancellation.stop_requested()) { return std::unexpected(Error::Cancelled); }
  const auto proxy = admittedProxy(plan, options);
  if (!proxy) { return std::unexpected(Error::Policy); }
  if (release.expiresAt() <= QDateTime::currentSecsSinceEpoch()) { return std::unexpected(Error::Expired); }
  const auto& expected = release.asset();
  if (expected.bytes <= 0 || expected.bytes > maximumAsset) { return std::unexpected(Error::AssetIdentity); }

  const QDeadlineTimer limit(options.deadline.count());
  QNetworkAccessManager manager;
  manager.setProxy(*proxy);
  auto bytes = download(manager, expected.url, expected.bytes, options, limit, Error::AssetIdentity);
  if (!bytes) { return bytes; }
  if (release.expiresAt() <= QDateTime::currentSecsSinceEpoch()) { return std::unexpected(Error::Expired); }
  QBuffer body(&*bytes);
  if (!body.open(QIODevice::ReadOnly) || !verifyPluginReleaseAsset(body, expected)) {
    return std::unexpected(Error::AssetIdentity);
  }
  if (options.cancellation.stop_requested()) { return std::unexpected(Error::Cancelled); }
  return bytes;
}

} // namespace biglybtc::update
