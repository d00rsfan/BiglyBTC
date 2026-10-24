#include "../engine/torrent_creator.hpp"

#include <charconv>
#include <iostream>
#include <string_view>

// Narrow owned-fixture driver for the original Java decoder handoff. The
// production Host/UI provides its own options; this helper never imports/runs.
int main(int argc, char** argv) {
  using namespace biglybtc::engine;
  if (argc != 5) { std::cerr << "SOURCE OUTPUT PIECE_SIZE PRIVATE(0|1) required\n"; return 2; }
  CreateOptions options; options.source = argv[1]; options.output = argv[2];
  const std::string_view size(argv[3]);
  const auto parsed = std::from_chars(size.data(), size.data() + size.size(), options.pieceSize);
  if (parsed.ec != std::errc{} || parsed.ptr != size.data() + size.size()
      || (std::string_view(argv[4]) != "0" && std::string_view(argv[4]) != "1")) { return 2; }
  options.privateTorrent = std::string_view(argv[4]) == "1";
  options.comment = "Creator – π";
  options.trackerTiers = {{"http://127.0.0.1:19123/announce", "udp://127.0.0.1:19124/announce"},
                         {"https://127.0.0.1:19125/announce"}};
  const auto result = createTorrent(options);
  if (!result) { std::cerr << "CREATE_ERROR " << static_cast<int>(result.error().code) << '\n'; return 1; }
  if (result->publication != CreatePublication::Durable) { std::cerr << "CREATE_PUBLISHED_UNCERTAIN\n"; return 3; }
  std::cout << "CREATED\t" << result->infoHashV1.hex() << '\t' << result->totalBytes << '\t'
            << result->fileCount << '\t' << result->pieceSize << '\t' << result->pieceCount << '\n';
}
