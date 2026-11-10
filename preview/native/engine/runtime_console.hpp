// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <syncstream>
#include <sstream>
#include <cstdlib>
#include <mutex>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace biglybtc::engine::detail {
// One complete line per thread. Console diagnostics are deliberately local;
// tracker URLs may contain credentials, as requested for development debugging.
inline std::string consoleText(std::string_view input,std::size_t limit=20000){
  std::string out;const auto count=std::min(input.size(),limit);out.reserve(count);
  constexpr char hex[]="0123456789ABCDEF";
  for(std::size_t i=0;i<count;++i){const auto byte=static_cast<unsigned char>(input[i]);
    if(byte<32||byte==127){out+="\\x";out+=hex[byte>>4];out+=hex[byte&15];}else{out+=static_cast<char>(byte);}}
  if(input.size()>count){out+=" [truncated]";}return out;
}
class ConsoleLine final:public std::ostringstream {
public:
  explicit ConsoleLine(std::string_view event){
    *this<<std::format("{:%FT%T}Z",std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()))<<" BiglyBTC "<<event;
  }
  ConsoleLine(const ConsoleLine&)=delete;
  ~ConsoleLine(){try{
    const auto line=str();std::osyncstream(std::cout)<<line<<std::flush;
    // The development launcher sends stderr to its owner-private local log.
    // Keep stdout visible and mirror only when that launcher explicitly asks.
    const auto* mirror=std::getenv("BIGLYBTC_CONSOLE_MIRROR_STDERR");
    if(mirror&&std::string_view(mirror)=="1"){
      static std::mutex mutex;std::lock_guard guard(mutex);
#ifndef _WIN32
      struct stat state{};
      if(::fstat(STDERR_FILENO,&state)==0&&S_ISREG(state.st_mode)&&state.st_uid==::geteuid()&&state.st_nlink==1&&state.st_size>4*1024*1024){if(::ftruncate(STDERR_FILENO,0)!=0){return;}}
#endif
      std::osyncstream(std::clog)<<line<<std::flush;
    }
  }catch(...){}} // Diagnostics must not fail a torrent admission.
};
inline ConsoleLine runtimeConsole(std::string_view event){return ConsoleLine(event);}
}
