# Requested Azureus maintenance daily

Source `1246f452de04534badfe87607bc5eae2316a9955`, repository tree `c867ed6bc27b04bfe15eb9e6ce346c3395b34c1a`. Frozen application SHA256
`13c803b5cc4b394f75e17b13b4dd15db83ac12fdbce43461af67d4a44312bef5`; SDK SHA256
`8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3` unchanged. GCC16/C++23/Qt6.12.0.

Runtime hashes and loader dependency resolution pass. No live profile was opened,
copied, backed up or written, and no client was restarted. Only the exact inactive
previous executable is retired under its exclusive manifest lock; an active
process retains its image until exit. Both stable launch aliases select this
image. After ordinary Exit from the current client:

    dist/BiglyBTC-daily-use --profile "$HOME/.biglybt"

This image includes the forced-seed Pause/Resume/Stop and builtin tag-label
repairs, cache/query/survey/signed-block maintenance and healthy saved-address
startup compatibility. Real-profile/desktop acceptance remains manual. Whole
F09 and hot address rebinding remain open. Development pauses after this daily
at the user's explicit request; no next chapter is started.
