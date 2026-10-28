# P04 actual native library and local command shell

- State in_progress; owner Codex; self review only.
- Bounded desktop integration, not full P04 or UI parity.
- Inputs: original GlobalManagerImpl inventory, CachedStateWrapper cache export,
  DownloadManagerState display attributes; source baseline
  `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; native executor/profile/local engine.
- Writes native core/adapters/UI/app/tests and migration records only.
- All executions offscreen with generated profiles; no normal desktop windows or
  actual profile access while the user's BiglyBT runs.

Read the actual saved inventory and optional gzip state cache into owned value
snapshots without scanning every torrent state or touching payloads. Preserve
original files; missing/bad cache is a reported optional metadata limitation.
Display real names, cached sizes, saved progress/counters and policy states.
Keep stable identities/generations across sorting, selection and refresh; reject
stale commands. Do no profile/network work on the GUI thread.

Compose BiglyBTC's first real Qt6.12 desktop shell with the same native services
as the headless runner: a bounded serial worker owns the profile lease, queries
and continuation command; UI receives owned queued results. Continue/cancel a
selected generated linear workload through an explicitly owned local peer.
Window teardown cancels owned work and leaves Java-compatible recoverable state.
Tests cover real generated snapshot display, lock ownership, stale actions,
completion/error/cancellation and teardown, plus offscreen smoke/screenshot.

Reuse qualified UI techniques and bounded graph history; never ship synthetic
peer/rate/ratio/category values as real data. Missing values remain unknown.
Full detail views, settings/actions, plugin pilots, localization/charset and
real-display/platform/accessibility qualification remain explicit later work.
