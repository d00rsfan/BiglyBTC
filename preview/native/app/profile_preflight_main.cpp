#include "profile_preflight.hpp"
#include <iostream>
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  if(argc!=2){std::cerr<<"Usage: biglybtc-profile-check ABSOLUTE_PROFILE\n";return 2;}
  auto profile = std::filesystem::path(argv[1]).make_preferred();
  return biglybtc::app::checkExistingProfile(profile, std::cout);
}
