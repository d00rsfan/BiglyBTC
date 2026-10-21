// Separate owned child-process negative fixture, used ONLY through LD_PRELOAD.
// The guard refuses instrumentation; this is not an alternate trusted provider.
extern "C" const char* versionInterposer() asm("_ZN10libtorrent7versionEv");
extern "C" const char* versionInterposer() { return "2.0.12"; }
