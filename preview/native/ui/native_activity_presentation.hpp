#pragma once
#include <memory>

namespace biglybtc::ui {
class NativeActivitySnapshot;
using NativeActivityPresentation = std::shared_ptr<const NativeActivitySnapshot>;
}
