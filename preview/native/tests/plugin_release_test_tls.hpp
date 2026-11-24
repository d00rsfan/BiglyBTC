// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_test_signing.hpp"

#include <QList>
#include <QMap>
#include <QNetworkProxy>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <functional>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

namespace biglybtc::update::test {
struct TestCertificate {
  QSslCertificate certificate;
  QSslKey key;
};

inline TestCertificate generatedCertificate() {
  const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", 2048), EVP_PKEY_free);
  const std::unique_ptr<X509, decltype(&X509_free)> x509(X509_new(), X509_free);
  require(key && x509 && X509_set_version(x509.get(), 2) == 1 &&
      ASN1_INTEGER_set(X509_get_serialNumber(x509.get()), 1) == 1 &&
      X509_gmtime_adj(X509_getm_notBefore(x509.get()), -60) &&
      X509_gmtime_adj(X509_getm_notAfter(x509.get()), 3600) && X509_set_pubkey(x509.get(), key.get()) == 1,
      "Generate an ephemeral fixture certificate");
  auto* name = X509_get_subject_name(x509.get());
  require(name && X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
      reinterpret_cast<const unsigned char*>("github.com"), -1, -1, 0) == 1 &&
      X509_set_issuer_name(x509.get(), name) == 1, "Set only the generated TLS identity");
  X509V3_CTX context{};
  X509V3_set_ctx(&context, x509.get(), x509.get(), nullptr, nullptr, 0);
  const auto extension = [&](int id, const char* value) {
    const std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> ext(
        X509V3_EXT_nconf_nid(nullptr, &context, id, value), X509_EXTENSION_free);
    return ext && X509_add_ext(x509.get(), ext.get(), -1);
  };
  require(extension(NID_subject_alt_name, "DNS:github.com,DNS:release-assets.fixture") &&
      extension(NID_basic_constraints, "critical,CA:TRUE") && X509_sign(x509.get(), key.get(), EVP_sha256()) > 0,
      "Sign the generated proxy fixture authority");

  const std::unique_ptr<BIO, decltype(&BIO_free)> certificatePem(BIO_new(BIO_s_mem()), BIO_free);
  const std::unique_ptr<BIO, decltype(&BIO_free)> keyPem(BIO_new(BIO_s_mem()), BIO_free);
  require(certificatePem && keyPem && PEM_write_bio_X509(certificatePem.get(), x509.get()) == 1 &&
      PEM_write_bio_PrivateKey(keyPem.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1,
      "Represent the generated fixture in memory");
  const auto bytes = [](BIO* file) {
    char* data = nullptr;
    const auto size = BIO_get_mem_data(file, &data);
    require(size > 0, "Read the fixture identity");
    return QByteArray(data, size);
  };
  return {QSslCertificate(bytes(certificatePem.get())), QSslKey(bytes(keyPem.get()), QSsl::Rsa)};
}

class OwnedConnectProxy final : public QTcpServer {
public:
  enum class Mode { Good, ApprovedRedirect, DeniedRedirect, OtherRepository, WrongBytes, Oversize, Silent };

  OwnedConnectProxy(QSslConfiguration tls, QByteArray bytes, bool socks = false)
      : tls_(std::move(tls)), bytes_(std::move(bytes)), socks_(socks) {
    setProxy(QNetworkProxy::NoProxy);
    require(listen(QHostAddress::LocalHost, 0), "Own the isolated proxy");
  }

  Mode mode = Mode::Good;
  QList<QByteArray> destinations;
  QList<QByteArray> requests;
  QList<std::pair<QByteArray, QByteArray>> socksCredentials;
  QMap<QByteArray, QByteArray> documents;
  std::function<void(QByteArrayView)> requestObserved;
  std::function<void()> connectObserved;

protected:
  void incomingConnection(qintptr descriptor) override {
    auto* socket = new QSslSocket(this);
    if (!socket->setSocketDescriptor(descriptor)) { socket->deleteLater(); return; }
    socket->setSslConfiguration(tls_);
    connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
    auto input = std::make_shared<QByteArray>();
    connect(socket, &QSslSocket::readyRead, this, [this, socket, input, phase = SocksPhase::Greeting]() mutable {
      *input += socket->readAll();
      if (socks_ && !socket->isEncrypted()) {
        serveSocks(*socket, *input, phase);
        return;
      }
      const auto header = input->indexOf("\r\n\r\n");
      if (header < 0) { return; }
      const auto line = input->first(input->indexOf("\r\n"));
      input->clear();
      if (!socket->isEncrypted()) {
        if (!line.startsWith("CONNECT ")) { socket->abort(); return; }
        const auto space = line.indexOf(' ', 8);
        const auto address = line.mid(8, space - 8);
        destinations.push_back(address);
        if (connectObserved) { connectObserved(); }
        if (mode == Mode::Silent) { return; }
        if (address != "github.com:443" && address != "release-assets.fixture:443") {
          socket->abort();
          return;
        }
        socket->write("HTTP/1.1 200 Connection Established\r\n\r\n");
        socket->flush();
        socket->startServerEncryption();
        return;
      }

      requests.push_back(line);
      if (requestObserved) { requestObserved(line); }
      if (line.startsWith("GET /owner/")) {
        if (mode == Mode::ApprovedRedirect) {
          send(*socket, "302 Found", "Location: https://release-assets.fixture/archive?generated=1\r\n", {});
          return;
        }
        if (mode == Mode::DeniedRedirect) {
          send(*socket, "302 Found", "Location: https://foreign.fixture/archive\r\n", {});
          return;
        }
      }
      if (mode == Mode::OtherRepository) {
        send(*socket, "302 Found", "Location: https://github.com/other/project/releases/latest/download/catalogue.json\r\n", {});
        return;
      }
      if (const auto document = documents.constFind(line); document != documents.cend()) {
        send(*socket, "200 OK", {}, *document);
        return;
      }
      auto data = bytes_;
      if (mode == Mode::WrongBytes) { data[70000] ^= 1; }
      if (mode == Mode::Oversize) { data += '!'; }
      send(*socket, "200 OK", {}, data);
    });
  }

private:
  enum class SocksPhase { Greeting, Authentication, Connection, Tls };

  void serveSocks(QSslSocket& socket, QByteArray& input, SocksPhase& phase) {
    const auto byte = [&](qsizetype index) { return static_cast<unsigned char>(input.at(index)); };
    if (phase == SocksPhase::Greeting) {
      if (input.size() < 2) { return; }
      const qsizetype length = byte(1);
      if (input.size() < 2 + length) { return; }
      if (byte(0) != 5 || !input.mid(2, length).contains(char{2})) { socket.abort(); return; }
      socket.write(QByteArray("\x05\x02", 2));
      input.remove(0, 2 + length);
      phase = SocksPhase::Authentication;
    } else if (phase == SocksPhase::Authentication) {
      if (input.size() < 2) { return; }
      const qsizetype user = byte(1);
      if (input.size() < 3 + user) { return; }
      const qsizetype pass = byte(2 + user);
      if (input.size() < 3 + user + pass) { return; }
      const auto credentials = std::pair{input.mid(2, user), input.mid(3 + user, pass)};
      socksCredentials.push_back(credentials);
      if (byte(0) != 1 || credentials.first != "fixture-client" || credentials.second != "fixture-token") {
        socket.write(QByteArray("\x01\x01", 2));
        socket.disconnectFromHost();
        return;
      }
      socket.write(QByteArray("\x01\x00", 2));
      input.remove(0, 3 + user + pass);
      phase = SocksPhase::Connection;
    } else if (phase == SocksPhase::Connection) {
      if (input.size() < 5) { return; }
      if (byte(0) != 5 || byte(1) != 1 || byte(2) != 0 || byte(3) != 3) { socket.abort(); return; }
      const qsizetype length = byte(4);
      if (input.size() < 7 + length) { return; }
      const auto name = input.mid(5, length);
      const auto port = byte(5 + length) * 256 + byte(6 + length);
      destinations.push_back(name + ':' + QByteArray::number(port));
      if ((name != "github.com" && name != "release-assets.fixture") || port != 443) { socket.abort(); return; }
      socket.write(QByteArray("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10));
      input.remove(0, 7 + length);
      phase = SocksPhase::Tls;
      socket.flush();
      socket.startServerEncryption();
    }
  }

  static void send(QSslSocket& target, QByteArrayView status, QByteArrayView headers, const QByteArray& body) {
    QByteArray response = "HTTP/1.1 " + status.toByteArray() + "\r\n";
    response += headers;
    response += "Connection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    target.write(response);
    target.write(body);
    target.disconnectFromHost();
  }

  QSslConfiguration tls_;
  QByteArray bytes_;
  bool socks_;
};

} // namespace biglybtc::update::test
