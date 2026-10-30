#include <QPointer>
#include "library_window.hpp"
#include "session_bridge.hpp"
#include "country_provider_binding.hpp"
#include "country_approval.hpp"
#include "scheduler_approval.hpp"
#include "scheduler_service.hpp"
#include "wayland_console.hpp"

#include <QApplication>

#include <charconv>
#include <clocale>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  const bool normal = argc >= 2 && std::string_view(argv[1]) == "--profile";
  const bool local = argc >= 2 && std::string_view(argv[1]) == "--experimental-local";
  if ((!normal && !local) || (normal && argc < 3) || (local && argc < 4)) {
    std::cerr << "Usage: BiglyBTC --profile ABSOLUTE_PROFILE [--country-db ABSOLUTE_DIRECTORY] [--private-i2p I2CP_PORT]\n"
                 "       BiglyBTC --experimental-local GENERATED_PROFILE OWNED_PEER_PORT [--country-db ABSOLUTE_DIRECTORY] [--private-i2p I2CP_PORT]\n";
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
  std::optional<biglybtc::app::I2pServiceOptions> i2pOptions;
  for(int option=countryOption;option<argc;option+=2){
    if(option+1>=argc){std::cerr<<"Missing option value\n";return 2;}
    if(std::string_view(argv[option])=="--country-db"&&countryDatabase.empty()){
      countryDatabase=argv[option+1];if(!countryDatabase.is_absolute()){std::cerr<<"Country database needs an absolute directory\n";return 2;}
    }else if(std::string_view(argv[option])=="--private-i2p"&&!i2pOptions){
#if BIGLYBTC_HOST_I2P_OWNER
      unsigned int routerPort=0;const std::string_view value(argv[option+1]);
      const auto parsed=std::from_chars(value.data(),value.data()+value.size(),routerPort);
      if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||!routerPort||routerPort>65535){std::cerr<<"Use a numeric owned loopback I2CP port\n";return 2;}
      i2pOptions.emplace();i2pOptions->routerPort=static_cast<unsigned short>(routerPort);
#else
      std::cerr<<"This build has no privately approved I2P provider\n";return 2;
#endif
    }else{std::cerr<<"Unknown or repeated option\n";return 2;}
  }

  try {
    if (!std::setlocale(LC_CTYPE, "")) { throw std::runtime_error("Unable to initialize the filesystem character locale"); }
    biglybtc::app::detail::installWaylandConsoleFilter();
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
    biglybtc::app::SessionBridge bridge(nullptr, std::move(countryFactory), {}, std::move(schedulerFactory),std::move(i2pOptions));
    using Window = biglybtc::ui::LibraryWindow;
    using Bridge = biglybtc::app::SessionBridge;
    QObject::connect(&bridge,&Bridge::localRatingReady,&window,&Window::setLocalRating);
    QObject::connect(&bridge,&Bridge::localRatingSaved,&window,&Window::setLocalRatingSaved);
    QObject::connect(&window,&Window::localRatingRequested,&bridge,
      [owner=QPointer<Bridge>(&bridge),view=QPointer<Window>(&window)](biglybtc::ui::LocalRatingRequest request){
        if(owner&&view&&!owner->loadLocalRating(request)&&view){view->rejectLocalRatingRequest(request);}
      });
    QObject::connect(&window,&Window::localRatingSaveRequested,&bridge,
      [owner=QPointer<Bridge>(&bridge),view=QPointer<Window>(&window)](biglybtc::ui::LocalRatingSubmission value){
        if(owner&&view&&!owner->saveLocalRating(value)&&view){view->rejectLocalRatingSave(value);}
      });
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
    QObject::connect(&bridge, &Bridge::i2pReady, &window, &Window::setI2p);
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
    QObject::connect(&window, &Window::hybridCompanionRequested, &bridge, &Bridge::createHybridCompanion);
    QObject::connect(&window, &Window::filePolicyRequested, &bridge, &Bridge::filePolicy);
    QObject::connect(&window, &Window::fileCompactionRequested, &bridge, &Bridge::compactFile);
    QObject::connect(&window,&Window::fileOperationPlanRequested,&bridge,[&](biglybtc::ui::FileOperationRequest r){if(!bridge.prepareFileOperation(r)){window.rejectFileOperationRequest(r);}});
    QObject::connect(&window,&Window::fileOperationExecuteRequested,&bridge,[&](biglybtc::ui::FileOperationSubmission r){if(!bridge.executeFileOperation(r)){window.rejectFileOperationSubmission(r);}});
    QObject::connect(&window,&Window::fileOperationCancelRequested,&bridge,&Bridge::cancelFileOperation);
    QObject::connect(&window,&Window::torrentCreateRequested,&bridge,[&](biglybtc::ui::TorrentCreationRequest r){if(!bridge.createTorrent(r)){window.rejectTorrentCreationRequest(r);}});
    QObject::connect(&window,&Window::torrentCreateCancelRequested,&bridge,&Bridge::cancelTorrentCreation);
    QObject::connect(&bridge,&Bridge::fileOperationPrepared,&window,&Window::setFileOperationPlan);
    QObject::connect(&bridge,&Bridge::fileOperationFinished,&window,&Window::setFileOperationResult);
    QObject::connect(&bridge,&Bridge::torrentCreationProgress,&window,&Window::setTorrentCreationProgress);
    QObject::connect(&bridge,&Bridge::torrentCreated,&window,&Window::setTorrentCreationResult);
    QObject::connect(&window, &Window::torrentImportRequested, &bridge, &Bridge::torrentImport);
    QObject::connect(&window,&Window::torrentIntakeRequested,&bridge,[&](biglybtc::ui::TorrentIntakeRequest r){if(!bridge.previewTorrentIntake(r)){window.rejectTorrentIntake(r);}});
    QObject::connect(&window,&Window::torrentIntakeSubmitted,&bridge,[&](biglybtc::ui::TorrentIntakeSubmission r){if(!bridge.importTorrentFiles(r)){window.rejectTorrentIntakeSubmission(r);}});
    QObject::connect(&bridge,&Bridge::torrentIntakeReady,&window,&Window::setTorrentIntake);
    QObject::connect(&bridge,&Bridge::torrentIntakeFinished,&window,&Window::setTorrentIntakeFinished);
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
