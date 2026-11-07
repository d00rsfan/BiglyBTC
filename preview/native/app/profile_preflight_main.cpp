#include "profile_preflight.hpp"
#include <iostream>
int main(int argc,char** argv) {
  if(argc!=2){std::cerr<<"Usage: biglybtc-profile-check ABSOLUTE_PROFILE\n";return 2;}
  return biglybtc::app::checkExistingProfile(argv[1],std::cout);
}
