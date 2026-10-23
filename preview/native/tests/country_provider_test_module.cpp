// Synthetic ABI-policy fixture ONLY. Not an authenticated production provider,
// real MMDB lookup, geographic oracle or executable Country-service proof.
#include "biglybtc/plugin.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <new>
namespace {
struct State{int directory;};
char state(State* s) noexcept {
  const int fd=::openat(s->directory,"provider-state",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
  if(fd<0){return 'M';}char c=0;const auto n=::read(fd,&c,1);::close(fd);return n==1?c:'M';
}
bt_status create(const bt_plugin_options_v1* options,void** out) noexcept {
  if(!options||!out){return BT_INVALID;}*out=nullptr;const int fd=::fcntl(options->plugin_directory_fd,F_DUPFD_CLOEXEC,3);
  if(fd<0){return BT_IO;}auto* s=new(std::nothrow) State{fd};if(!s){::close(fd);return BT_FAILED;}*out=s;return BT_OK;
}
bt_status start(void* p) noexcept{return p?BT_OK:BT_INVALID;}
bt_status call(void* instance,std::uint32_t op,const void* request,std::uint32_t size,void* response,std::uint32_t output) noexcept {
  auto* s=static_cast<State*>(instance);if(!s||!request||!response){return BT_INVALID;}
  if(op==BT_LOCATION_DATABASE_STATUS&&size==sizeof(bt_location_database_request_v1)&&output==sizeof(bt_location_database_result_v1)){
    const auto c=state(s);const bt_location_database_result_v1 result{c=='A'?1U:0U,0};std::memcpy(response,&result,sizeof(result));return c=='A'?BT_OK:BT_NOT_FOUND;
  }
  if(op==BT_LOCATION_LOOKUP){return BT_NOT_FOUND;} // New facade must use checked lookup.
  if(op==BT_LOCATION_LOOKUP_CHECKED&&size==sizeof(bt_location_request_v1)&&output==sizeof(bt_location_result_v1)){
    if(state(s)!='A'){return BT_IO;}bt_location_request_v1 input{};std::memcpy(&input,request,sizeof(input));
    if(input.address[3]==0){return BT_NOT_FOUND;}if(input.address[3]==255){return BT_IO;}
    bt_location_result_v1 result{};std::memcpy(result.iso3166,"US",3);std::memcpy(result.country,"Synthetic fixture",18);
    std::memcpy(response,&result,sizeof(result));return BT_OK;
  }
  return BT_UNSUPPORTED;
}
void stop(void*) noexcept{}
void destroy(void* p) noexcept{if(p){auto* s=static_cast<State*>(p);::close(s->directory);delete s;}}
const bt_plugin_v1 Api{sizeof(bt_plugin_v1),BT_PLUGIN_ABI_V1,"azlocprov","country.test",BT_CAP_LOCATION,create,start,call,stop,destroy};
}
extern "C" BT_PLUGIN_EXPORT const bt_plugin_v1* biglybtc_plugin_entry(std::uint32_t version){return version==BT_PLUGIN_ABI_V1?&Api:nullptr;}
