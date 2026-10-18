#pragma once

#include <memory>

namespace biglybtc::ui {

class EngineActivitySnapshot;
using EngineActivityPresentation = std::shared_ptr<const EngineActivitySnapshot>;

} // namespace biglybtc::ui
