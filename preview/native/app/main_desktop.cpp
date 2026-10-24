#include "library_window.hpp"
#include "session_bridge.hpp"
#include "country_provider_binding.hpp"
#include "country_approval.hpp"
#include "scheduler_approval.hpp"
#include "scheduler_service.hpp"

#include <QApplication>

#include <charconv>
#include <clocale>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  const bool normal = argc >= 2 && std::string_view(argv[1]) == "--profile";
  const bool local = argc >= 2 && std::string_view(argv[1]) == "--experimental-local";
  if ((!normal && !local) || (normal && argc != 3 && argc != 5) || (local && argc != 4 && argc != 6)) {
    std::cerr << "Usage: BiglyBTC --profile ABSOLUTE_PROFILE [--country-db ABSOLUTE_DIRECTORY]\n"
                 "       BiglyBTC --experimental-local GENERATED_PROFILE OWNED_PEER_PORT [--country-db ABSOLUTE_DIRECTORY]\n";
    return 2;
  }
  const std::filesystem::path profile(argv[2]);
  const auto normalized = profile.lexically_normal();
  std::error_code pathError;
  const auto canonical = std::filesystem::canonical(profile, pathError);
  if (pathError || !profile.is_absolute() || canonical != profile || normalized != profile) {
    std::cerr << "Use an existing absolute profile directory without symbolic links\n";
    return 2;
  }
  unsigned int port = 0;
  if (local) {
    auto part = normalized.begin();
    bool generated = part != normalized.end() && *part++ == "/";
    generated = generated && part != normalized.end() && *part++ == "tmp";
    generated = generated && part != normalized.end() && part->string().starts_with("biglybtc-");
    const std::string_view portText(argv[3]);
    const auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), port);
    if (!generated || parsed.ec != std::errc{} || parsed.ptr != portText.data() + portText.size() || port > 65535) {
      std::cerr << "Use a generated /tmp/biglybtc-* profile and an owned endpoint (0 for an offline preview)\n";
      return 2;
    }
  }
  std::filesystem::path countryDatabase;
  const int countryOption = normal ? 3 : 4;
  if (argc > countryOption) {
    countryDatabase = argv[countryOption + 1];
    if (std::string_view(argv[countryOption]) != "--country-db" || !countryDatabase.is_absolute()) {
      std::cerr << "Country data requires --country-db ABSOLUTE_DIRECTORY\n";
      return 2;
    }
  }
  try {
    if (!std::setlocale(LC_CTYPE, "")) { throw std::runtime_error("Unable to initialize the filesystem character locale"); }
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("BiglyBTC"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    biglybtc::ui::LibraryWindow window;
    window.setOfflinePreview(!normal && port == 0);
    if (!normal && port == 0) {
      window.setWindowTitle(QStringLiteral("BiglyBTC — Offline preview"));
    }
    biglybtc::app::SessionBridge::CountryProviderFactory countryFactory;
    if (!countryDatabase.empty()) {
      const auto executable = std::filesystem::canonical("/proc/self/exe");
      auto module = executable.parent_path().parent_path() / "lib/BiglyBTC-plugin-azlocprov.so";
      // Build-tree preview uses the same compiled approved module beside the executable.
      if (!std::filesystem::exists(module)) { module = executable.parent_path() / "BiglyBTC-plugin-azlocprov.so"; }
      const biglybtc::storage::CountryModuleApproval approval{biglybtc::app::country_bundle::Sha256,
          biglybtc::app::country_bundle::Bytes, biglybtc::app::country_bundle::Version};
      countryFactory = [module, approval, countryDatabase](const biglybtc::storage::ProfileLease& lease) {
        auto provider = biglybtc::storage::loadApprovedCountryProvider(lease, module, approval, countryDatabase);
        if (!provider) { std::cerr << "Country provider unavailable: " << provider.error().message << '\n'; return std::unique_ptr<biglybtc::storage::CountryProvider>{}; }
        return std::move(*provider);
      };
    }
    const auto executable=std::filesystem::canonical("/proc/self/exe");
    auto schedulerModule=executable.parent_path().parent_path()/"lib/BiglyBTC-plugin-SpeedScheduler.so";
    if(!std::filesystem::exists(schedulerModule)){schedulerModule=executable.parent_path()/"BiglyBTC-plugin-SpeedScheduler.so";}
    const biglybtc::storage::SchedulerModuleApproval schedulerApproval{
        biglybtc::app::scheduler_bundle::Sha256,biglybtc::app::scheduler_bundle::Bytes,biglybtc::app::scheduler_bundle::Version};
    biglybtc::app::SessionBridge::SchedulerFactory schedulerFactory=[schedulerModule,schedulerApproval](const biglybtc::storage::ProfileLease& lease){
      auto loaded=biglybtc::storage::loadApprovedSchedulerService(lease,schedulerModule,schedulerApproval);
      if(!loaded){throw std::runtime_error(loaded.error().message);}return std::move(*loaded);
    };
    biglybtc::app::SessionBridge bridge(nullptr, std::move(countryFactory), {}, std::move(schedulerFactory));
    using Window = biglybtc::ui::LibraryWindow;
    using Bridge = biglybtc::app::SessionBridge;
    QObject::connect(&bridge,&Bridge::schedulerReady,&window,&Window::setScheduler);
    QObject::connect(&bridge,&Bridge::schedulerSaved,&window,&Window::setSchedulerSaved);
    QObject::connect(&window,&Window::schedulerRequested,&bridge,[&](biglybtc::ui::SchedulerRequest request){
      if(!bridge.loadScheduler(request)){window.rejectSchedulerRequest(request);}
    });
    QObject::connect(&window,&Window::schedulerSaveRequested,&bridge,[&](biglybtc::ui::SchedulerSubmission submission){
      if(!bridge.saveScheduler(submission)){window.rejectSchedulerSave(submission);}
    });
    QObject::connect(&bridge, &Bridge::optionsReady, &window, &Window::setOptions);
    QObject::connect(&bridge, &Bridge::optionsSaved, &window, &Window::setOptionsSaved);
    QObject::connect(&bridge, &Bridge::intakeDefaultsReady, &window, &Window::setIntakeDefaults);
    QObject::connect(&window, &Window::optionsRequested, &bridge, [&](biglybtc::ui::OptionsRequest request) {
      if (!bridge.loadOptions(request)) { window.rejectOptionsRequest(request); }
    });
    QObject::connect(&window, &Window::optionsSaveRequested, &bridge, [&](biglybtc::ui::OptionsSubmission submission) {
      if (!bridge.saveOptions(submission)) { window.rejectOptionsSave(submission); }
    });
    QObject::connect(&bridge, &Bridge::snapshotReady, &window, &Window::setPresentation);
    QObject::connect(&bridge, &Bridge::progressReady, &window, &Window::setTelemetry);
    QObject::connect(&bridge, &Bridge::detailsReady, &window, &Window::setDetails);
    QObject::connect(&bridge, &Bridge::savedServicesReady, &window, &Window::setSavedServices);
    QObject::connect(&bridge, &Bridge::statisticsReady, &window, &Window::setStatistics);
    QObject::connect(&bridge, &Bridge::historyChanged, &window, &Window::setHistoryRevision);
    QObject::connect(&bridge, &Bridge::historyReady, &window, &Window::setHistory);
    QObject::connect(&bridge, &Bridge::historyWeekReady, &window, &Window::setHistoryWeek);
    QObject::connect(&bridge, &Bridge::historyMonthReady, &window, &Window::setHistoryMonth);
    QObject::connect(&bridge, &Bridge::historyRangeReady, &window, &Window::setHistoryRange);
    QObject::connect(&bridge, &Bridge::engineStatisticsReady, &window, &Window::setEngineStatistics);
    QObject::connect(&bridge, &Bridge::columnLayoutSessionReady, &window, &Window::beginColumnLayoutSession);
    QObject::connect(&bridge, &Bridge::columnLayoutsReady, &window, &Window::setColumnLayouts);
    QObject::connect(&bridge, &Bridge::columnLayoutSaved, &window, &Window::setColumnLayoutSaved);
    QObject::connect(&bridge, &Bridge::busyChanged, &window, &Window::setBusy);
    QObject::connect(&bridge, &Bridge::messageReady, &window, &Window::setMessage);
    QObject::connect(&window, &Window::refreshRequested, &bridge, &Bridge::refresh);
    QObject::connect(&window, &Window::downloadActionRequested, &bridge, &Bridge::downloadAction);
    QObject::connect(&window, &Window::filePolicyRequested, &bridge, &Bridge::filePolicy);
    QObject::connect(&window, &Window::torrentImportRequested, &bridge, &Bridge::torrentImport);
    QObject::connect(&window, &Window::magnetImportRequested, &bridge, &Bridge::magnetImport);
    QObject::connect(&window, &Window::continueRequested, &bridge, &Bridge::continueDownload);
    QObject::connect(&window, &Window::detailsRequested, &bridge, &Bridge::details);
    QObject::connect(&window, &Window::savedServicesRequested, &bridge, &Bridge::savedServices);
    QObject::connect(&window, &Window::statisticsRequested, &bridge, &Bridge::statistics);
    QObject::connect(&window, &Window::historyRequested, &bridge, &Bridge::history);
    QObject::connect(&window, &Window::historyWeekRequested, &bridge, &Bridge::historyWeek);
    QObject::connect(&window, &Window::historyMonthRequested, &bridge, &Bridge::historyMonth);
    QObject::connect(&window, &Window::historyRangeRequested, &bridge, &Bridge::historyRange);
    QObject::connect(&window, &Window::columnLayoutsRequested, &bridge, &Bridge::columnLayouts);
    QObject::connect(&window, &Window::columnLayoutSaveRequested, &bridge, &Bridge::saveColumnLayout);
    QObject::connect(&window, &Window::cancelRequested, &bridge, &Bridge::cancel);
    QObject::connect(&window, &Window::manualTagStatisticsRequested, &bridge, &Bridge::manualTagStatistics);
    QObject::connect(&bridge, &Bridge::manualTagStatisticsReady, &window, &Window::setManualTagStatistics);
    QObject::connect(&window, &Window::countryStatisticsRequested, &bridge, &Bridge::countryStatistics);
    QObject::connect(&bridge, &Bridge::countryStatisticsReady, &window, &Window::setCountryStatistics);
    if (normal) { bridge.openNormal(profile); }
    else { bridge.open(profile, static_cast<unsigned short>(port)); }
    window.enableSessionCloseHandshake();
    QObject::connect(&bridge, &Bridge::nativeSessionReady, &window, &Window::setNativeSession);
    QObject::connect(&bridge, &Bridge::overallReady, &window, &Window::setOverall);
    QObject::connect(&bridge, &Bridge::runtimeMetricsReady, &window, &Window::setRuntimeMetrics);
    QObject::connect(&bridge, &Bridge::azureusStatisticsReady, &window, &Window::setAzureusStatistics);
    QObject::connect(&bridge, &Bridge::liveSpeedManagerReady, &window, &Window::setLiveSpeedManager);
    QObject::connect(&window, &Window::markRequested, &bridge, &Bridge::markStatistics);
    QObject::connect(&window, &Window::sessionCloseRequested, &bridge, [&] {
      if (!bridge.closeSession()) {
        window.retrySessionClose();
        window.setMessage(QStringLiteral("Cannot close the native session yet; retry closing the window"));
      }
    });
    QObject::connect(&bridge, &Bridge::sessionClosed, &window, [&](quint64, bool, bool) {
      window.completeSessionClose();
    });
    QObject::connect(&bridge, &Bridge::sessionCloseFailed, &window, &Window::retrySessionClose);
    window.show();
    return application.exec();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
