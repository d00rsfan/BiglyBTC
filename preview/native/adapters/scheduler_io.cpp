#include "scheduler_io.hpp"
#include "profile_file_snapshot.hpp"
#ifdef _WIN32
#include "../platform/windows_file_owner.hpp"
#endif
#include <openssl/evp.h>
#include <array>
#include <memory>

namespace biglybtc::storage::scheduler_detail {
[[noreturn]] void fail(ErrorCode code, const char* message) { throw Failure{{code, message}}; }
void checkStop(std::stop_token stop) {
  if (stop.stop_requested()) { fail(ErrorCode::Cancelled, "Scheduler operation cancelled before mutation"); }
}

namespace {
bool generallyWritable(StateHandle handle, const StateStamp& stamp) {
#ifdef _WIN32
  static_cast<void>(stamp);
  PACL permissions = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const auto status = ::GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
      nullptr, nullptr, &permissions, nullptr, &raw);
  const std::unique_ptr<void, decltype(&::LocalFree)> descriptor(raw, ::LocalFree);
  if (status != ERROR_SUCCESS || !permissions) { return true; }
  constexpr DWORD write = GENERIC_ALL | GENERIC_WRITE | DELETE | WRITE_DAC | WRITE_OWNER
      | FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA;
  for (DWORD index = 0; index < permissions->AceCount; ++index) {
    void* item = nullptr;
    if (!::GetAce(permissions, index, &item)) { return true; }
    const auto* header = static_cast<const ACE_HEADER*>(item);
    if (header->AceType == ACCESS_DENIED_ACE_TYPE || (header->AceFlags & INHERIT_ONLY_ACE)) { continue; }
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) { return true; }
    const auto* allow = static_cast<const ACCESS_ALLOWED_ACE*>(item);
    auto sid = const_cast<DWORD*>(&allow->SidStart);
    if ((allow->Mask & write) && (::IsWellKnownSid(sid, WinWorldSid) || ::IsWellKnownSid(sid, WinAnonymousSid)
        || ::IsWellKnownSid(sid, WinAuthenticatedUserSid))) { return true; }
  }
  return false;
#else
  static_cast<void>(handle);
  return (stamp.mode & 0002) != 0;
#endif
}
}

Raw read(StateHandle parent, const char* name, std::size_t maximum) {
  auto snapshot = detail::captureProfileFile(parent, name, maximum);
  if (!snapshot) { throw Failure{snapshot.error()}; }
  if (!snapshot->stamp) { return {}; }
  return {std::move(snapshot->bytes), *snapshot->stamp};
}

void unchanged(StateHandle parent, const char* name, const Raw& before, std::size_t maximum) {
  const auto now = read(parent, name, maximum);
  if (now.bytes != before.bytes || (before.bytes && !detail::sameStateSnapshot(before.identity, now.identity))) {
    fail(ErrorCode::Conflict, "Scheduler source changed; reload before editing");
  }
}

StateDescriptor directory(StateHandle parent, const char* name, bool missingAllowed) {
  auto fd = detail::openStateDirectory(parent, name);
  if (!fd) {
    if (missingAllowed && fd.error().code == ErrorCode::Missing) { return {}; }
    throw Failure{fd.error()};
  }
  const auto info = detail::stateStamp(fd->get());
  if (!info || !info->directory || !info->owned || !info->links || generallyWritable(fd->get(), *info)) {
    fail(ErrorCode::UnsafePath, "Scheduler directory must be owned, live and protected against arbitrary writes");
  }
  directoryIdentity(parent, name, fd->get());
  return std::move(*fd);
}

void directoryIdentity(StateHandle parent, const char* name, StateHandle fd) {
  const auto held = detail::stateStamp(fd);
  const auto named = detail::stateStampAt(parent, name);
  if (!held || !named || !*named || !(*named)->links || !detail::sameStateDirectory(*held, **named)) {
    fail(ErrorCode::Conflict, "Scheduler directory was replaced");
  }
}

Raw replace(StateHandle parent, const char* name, const Raw& before, std::string_view value, std::size_t maximum) {
  if (value.size() > maximum) { fail(ErrorCode::InvalidFormat, "Scheduler output exceeds byte bound"); }
  unchanged(parent, name, before, maximum);
  const auto temporary = std::string(name) + ".BiglyBTC.saving";
  auto stage = detail::stageStateFile(parent, temporary, value, 0600);
  if (!stage) { throw Failure{stage.error()}; }
  const auto sync = detail::syncStateFile(stage->get());
  if (!sync) { throw Failure{sync.error()}; }
  const auto stamp = detail::stateStamp(stage->get());
  if (!stamp || !stamp->regular || !stamp->owned || stamp->links != 1 || stamp->size < 0
      || static_cast<std::uint64_t>(stamp->size) != value.size()) {
    fail(ErrorCode::Conflict, "Scheduler temporary identity changed");
  }
  unchanged(parent, name, before, maximum);
  const auto published = detail::publishStateFile(parent, temporary, name, stage->get(), before.bytes.has_value(), &*stamp);
  if (!published) { throw Failure{published.error()}; }
  *stage = StateDescriptor{};
  const auto parentSync = detail::syncStateDirectory(parent);
  if (!parentSync) { throw Failure{parentSync.error()}; }
  return read(parent, name, maximum);
}

void remove(StateHandle parent, const char* name, const Raw& before, std::size_t maximum) {
  unchanged(parent, name, before, maximum);
  if (!before.bytes) { return; }
  const auto retired = detail::retireStateFile(parent, name, before.identity);
  if (!retired) { throw Failure{retired.error()}; }
  const auto sync = detail::syncStateDirectory(parent);
  if (!sync) { throw Failure{sync.error()}; }
}

std::string sha256(StateHandle file,std::uint64_t expected) {
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!md||EVP_DigestInit_ex(md.get(),EVP_sha256(),nullptr)!=1){fail(ErrorCode::Io,"Cannot initialize scheduler artifact authentication");}
  std::array<char,65536> bytes{};std::uint64_t count=0;
  for(;;){const auto n=detail::readStateSequential(file,bytes.data(),bytes.size());if(n<0){fail(ErrorCode::Io,"Cannot read scheduler artifact");}if(!n){break;}
    const auto size=static_cast<std::uint64_t>(n);if(size>expected-count){fail(ErrorCode::Conflict,"Scheduler artifact grew during authentication");}
    count+=size;if(EVP_DigestUpdate(md.get(),bytes.data(),static_cast<std::size_t>(n))!=1){fail(ErrorCode::Io,"Cannot hash scheduler artifact");}}
  if(count!=expected){fail(ErrorCode::Conflict,"Scheduler artifact length differs from approval");}
  std::array<unsigned char,EVP_MAX_MD_SIZE> value{};unsigned size=0;
  if(EVP_DigestFinal_ex(md.get(),value.data(),&size)!=1||size!=32){fail(ErrorCode::Io,"Cannot finish scheduler artifact authentication");}
  constexpr char hex[]="0123456789abcdef";std::string out(64,'0');for(std::size_t i=0;i<32;++i){out[i*2]=hex[value[i]>>4];out[i*2+1]=hex[value[i]&15];}return out;
}
}
