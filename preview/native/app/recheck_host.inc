  struct RecheckTargetState {
    ui::Target displayed;
    core::TorrentHash hash;
    core::SavedDownload saved;
    std::optional<core::SavedPause> paused;
    bool continueAfter=false;
  };
  struct RecheckWork {
    ui::RecheckRequest request;
    std::vector<RecheckTargetState> targets;
    std::size_t index=0;
    quint64 sequence=0;
    bool scanStarted=false,retirementRequested=false,cancelled=false,cancelForwarded=false;
    std::optional<std::chrono::steady_clock::time_point> retirementDeadline;
  };
  std::unique_ptr<RecheckWork> recheckWork;
  std::set<std::string> recheckTargets;
