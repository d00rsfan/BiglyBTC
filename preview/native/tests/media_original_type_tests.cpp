// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_original_type.hpp"

#include <QString>
#include <iostream>

int main(int argc, char**) {
  using namespace biglybtc::media;
  if (argc == 2) {
    for (std::string name; std::getline(std::cin, name);) {
      const auto type = originalMediaType(QString::fromStdString(name));
      std::cout << type.primary << '\t' << type.alternative << '\t' << type.objectClass() << '\t'
          << type.objectClass(true) << '\n';
    }
    return 0;
  }
  const auto movie = originalMediaType(u"dir/movie.MKV");
  const auto television = originalMediaType(u"dir/clip.m2ts");
  const auto audio = originalMediaType(u"album.track.M4a");
  const auto picture = originalMediaType(u"dir/family.JpEg");
  const auto unknown = originalMediaType(u"dir.mp4/file");
  if (movie.kind != MediaKind::Video || movie.primary != "video/x-matroska" || movie.alternative != "video/x-mkv" ||
      movie.objectClass() != "object.item.videoItem.movie" || movie.objectClass(true) != "object.item.videoItem" ||
      television.primary != "video/MP2T" || audio.kind != MediaKind::Audio || audio.primary != "audio/mp4" ||
      picture.kind != MediaKind::Picture || picture.primary != "image/jpeg" ||
      unknown.kind != MediaKind::Unknown || unknown.primary != "unknown/unknown" ||
      originalMediaType(u"file.").kind != MediaKind::Unknown || originalMediaType(u"dir.mp4/").kind != MediaKind::Unknown) {
    std::cerr << "Original MIME or content class differs\n";
    return 1;
  }
  return 0;
}
