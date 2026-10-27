#include "../engine/torrent_creator.hpp"

#include <charconv>
#include <iostream>
#include <string_view>

// Narrow owned-fixture driver for the original Java decoder handoff. The
// production Host/UI provides its own options; this helper never imports/runs.
int main(int argc, char** argv) {
  using namespace biglybtc::engine;
  if (argc != 5 && argc != 6 && argc != 7) { std::cerr << "SOURCE OUTPUT PIECE_SIZE PRIVATE(0|1) required\n"; return 2; }
  CreateOptions options; options.source = argv[1]; options.output = argv[2];
  const std::string_view size(argv[3]);
  const auto parsed = std::from_chars(size.data(), size.data() + size.size(), options.pieceSize);
  if (parsed.ec != std::errc{} || parsed.ptr != size.data() + size.size()
      || (std::string_view(argv[4]) != "0" && std::string_view(argv[4]) != "1")) { return 2; }
  options.privateTorrent = std::string_view(argv[4]) == "1";
  if(argc>=6){const std::string_view format(argv[5]);if(format=="v1") options.format=CreateFormat::V1;
    else if(format=="v2") options.format=CreateFormat::V2;else if(format=="hybrid") options.format=CreateFormat::Hybrid;else return 2;}
  if(argc==7){if(std::string_view(argv[6])!="metadata-extras") return 2;
    options.sourceTag="owned π";options.urlSeeds={"https://seeds.invalid/payload"};
    options.httpSeeds={"http://seeds.invalid/script"};options.dhtBackup=false;options.extraHashes=true;}
  options.comment = "Creator – π";
  options.trackerTiers = {{"http://127.0.0.1:19123/announce", "udp://127.0.0.1:19124/announce"},
                         {"https://127.0.0.1:19125/announce"}};
  const auto result = createTorrent(options);
  if (!result) { std::cerr << "CREATE_ERROR " << static_cast<int>(result.error().code) << '\n'; return 1; }
  if (result->publication != CreatePublication::Durable) { std::cerr << "CREATE_PUBLISHED_UNCERTAIN\n"; return 3; }
  std::cout << "CREATED\t" << result->selectedHash.hex() << '\t' << result->totalBytes << '\t'
            << result->fileCount << '\t' << result->pieceSize << '\t' << result->pieceCount << '\n';
  if(argc>=6) std::cout<<"FULL_HASHES\t"<<(result->fullInfoHashV1?result->fullInfoHashV1->hex():"-")<<'\t'
      <<(result->fullInfoHashV2?result->fullInfoHashV2->hex():"-")<<'\n';
}
