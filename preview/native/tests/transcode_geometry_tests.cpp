// SPDX-License-Identifier: GPL-2.0-or-later
#include "transcode_profile.hpp"

#include <iostream>
#include <string_view>

using namespace biglybtc::media;

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--fixtures") {
    for (;;) {
      int width = 0;
      int height = 0;
      TranscodeProfile profile;
      std::size_t count = 0;
      if (!(std::cin >> width >> height >> profile.maxWidth >> profile.maxHeight >> profile.videoWidth >>
          profile.videoHeight >> profile.pixelMod >> profile.paddingMod >> profile.autoPadding >> count)) {
        return std::cin.eof() ? 0 : 1;
      }
      if (count > 64) { return 1; }
      for (std::size_t i = 0; i < count; ++i) {
        AspectRatio ratio;
        if (!(std::cin >> ratio.numerator >> ratio.denominator)) { return 1; }
        profile.aspectRatios.push_back(ratio);
      }
      const auto result = transcodeVideoGeometry(profile, width, height);
      if (!result) {
        std::cout << "invalid\n";
      } else {
        const auto display = result->display.value_or(AspectRatio{});
        std::cout << result->width << ' ' << result->height << ' ' << result->padX << ' ' << result->padY << ' ' <<
            display.numerator << ' ' << display.denominator << '\n';
      }
    }
  }
  TranscodeProfile bounded;
  bounded.maxWidth = 1280;
  bounded.maxHeight = 720;
  const auto landscape = transcodeVideoGeometry(bounded, 1920, 1080);
  const auto portrait = transcodeVideoGeometry(bounded, 640, 1440);
  if (!landscape || landscape->width != 1280 || landscape->height != 720 ||
      !portrait || portrait->width != 320 || portrait->height != 720) { return 1; }
  bounded.maxWidth = bounded.maxHeight = 0;
  bounded.videoWidth = 640;
  bounded.videoHeight = 480;
  bounded.paddingMod = 4;
  bounded.autoPadding = true;
  bounded.aspectRatios = {{4, 3}, {16, 9}};
  const auto wide = transcodeVideoGeometry(bounded, 1920, 800);
  const auto square = transcodeVideoGeometry(bounded, 800, 800);
  if (!wide || wide->width != 640 || wide->height != 360 || wide->padY != 60 ||
      !square || square->width != 480 || square->height != 480 || square->padX != 80) { return 1; }
  if (transcodeVideoGeometry(bounded, 0, 200)) { return 1; }
  bounded.pixelMod = 0;
  if (transcodeVideoGeometry(bounded, 300, 200)) { return 1; }
  std::cout << "PASS original transcode clipping, explicit size, aspect selection and padding\n";
}
