#include "library_window.hpp"
#include "session_bridge.hpp"

#include <QApplication>

#include <charconv>
#include <clocale>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 4 || std::string_view(argv[1]) != "--experimental-local") {
    std::cerr << "Usage: BiglyBTC --experimental-local GENERATED_PROFILE OWNED_PEER_PORT\n";
    return 2;
  }
  const std::filesystem::path profile(argv[2]);
  const auto normalized = profile.lexically_normal();
  auto part = normalized.begin();
  bool generated = part != normalized.end() && *part++ == "/";
  generated = generated && part != normalized.end() && *part++ == "tmp";
  generated = generated && part != normalized.end() && part->string().starts_with("biglybtc-");
  std::error_code pathError;
  const auto canonical = std::filesystem::canonical(profile, pathError);
  unsigned int port = 0;
  const std::string_view portText(argv[3]);
  const auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), port);
  if (!generated || pathError || canonical != profile || normalized != profile || parsed.ec != std::errc{}
      || parsed.ptr != portText.data() + portText.size() || port > 65535) {
    std::cerr << "Use a generated /tmp/biglybtc-* profile and an owned endpoint (0 for a read-only preview)\n";
    return 2;
  }
  try {
    if (!std::setlocale(LC_CTYPE, "")) { throw std::runtime_error("Unable to initialize the filesystem character locale"); }
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("BiglyBTC"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    biglybtc::ui::LibraryWindow window;
    window.setOfflinePreview(port == 0);
    if (port == 0) {
      window.setWindowTitle(QStringLiteral("BiglyBTC — Offline preview"));
    }
    biglybtc::app::SessionBridge bridge;
    using Window = biglybtc::ui::LibraryWindow;
    using Bridge = biglybtc::app::SessionBridge;
    QObject::connect(&bridge, &Bridge::snapshotReady, &window, &Window::setPresentation);
    QObject::connect(&bridge, &Bridge::progressReady, &window, &Window::setTelemetry);
    QObject::connect(&bridge, &Bridge::detailsReady, &window, &Window::setDetails);
    QObject::connect(&bridge, &Bridge::busyChanged, &window, &Window::setBusy);
    QObject::connect(&bridge, &Bridge::messageReady, &window, &Window::setMessage);
    QObject::connect(&window, &Window::refreshRequested, &bridge, &Bridge::refresh);
    QObject::connect(&window, &Window::continueRequested, &bridge, &Bridge::continueDownload);
    QObject::connect(&window, &Window::detailsRequested, &bridge, &Bridge::details);
    QObject::connect(&window, &Window::cancelRequested, &bridge, &Bridge::cancel);
    bridge.open(profile, static_cast<unsigned short>(port));
    window.show();
    return application.exec();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
