// SPDX-License-Identifier: GPL-2.0-or-later
#include "state_directory_listing.hpp"

#include <memory>

#ifdef _WIN32
#include "../platform/windows_file_owner.hpp"
#include <array>
#include <cstddef>
#else
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#endif

namespace biglybtc::storage::detail {
namespace {
std::unexpected<Error> listingError() {
  return std::unexpected(Error{ErrorCode::Io,"Cannot enumerate the held profile directory"});
}
#ifdef _WIN32
std::unexpected<Error> listingError(const char* operation,DWORD code) {
  return std::unexpected(Error{ErrorCode::Io,std::string(operation)+" failed (Win32 error "+std::to_string(code)+')'});
}
#endif
std::unexpected<Error> boundError() {
  return std::unexpected(Error{ErrorCode::Unsupported,"Profile directory listing exceeds the input bound"});
}
}

std::expected<std::vector<std::filesystem::path>, Error> listStateDirectoryNames(StateHandle directory, std::size_t maximum) {
  std::vector<std::filesystem::path> names;
#ifdef _WIN32
  // Use an independent cursor and authenticate the reopened directory before reading it.
  const auto path=stateDirectoryPath(directory);
  if(!path){return std::unexpected(path.error());}
  auto name=path->native();
  if(name.starts_with(L"\\\\")){name=L"\\\\?\\UNC\\"+name.substr(2);}
  else{name=L"\\\\?\\"+name;}
  const auto reopened=::CreateFileW(name.c_str(),FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(reopened==INVALID_HANDLE_VALUE){return listingError("Reopening the held profile directory",::GetLastError());}
  const StateDescriptor cursor(reopened);
  const auto held=stateStamp(directory),opened=stateStamp(cursor.get());
  if(!held||!opened||!opened->directory||!sameStateIdentity(*held,*opened)){
    return std::unexpected(Error{ErrorCode::Conflict,"Profile directory changed before enumeration"});
  }
  alignas(FILE_ID_BOTH_DIR_INFO) std::array<std::byte,32768> data;
  constexpr auto header=offsetof(FILE_ID_BOTH_DIR_INFO,FileName);
  auto infoType=FileIdBothDirectoryRestartInfo;
  for(;;){
    if(!::GetFileInformationByHandleEx(cursor.get(),infoType,data.data(),static_cast<DWORD>(data.size()))){
      const auto error=::GetLastError();
      if(error==ERROR_NO_MORE_FILES){return names;}
      return listingError("Enumerating the held profile directory",error);
    }
    infoType=FileIdBothDirectoryInfo;
    for(std::size_t offset=0;;){
      if(offset>data.size()-header){return listingError();}
      const auto* entry=reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(data.data()+offset);
      const auto length=entry->FileNameLength;
      if(length==0||length%sizeof(wchar_t)!=0||length>data.size()-offset-header){return listingError();}
      if(names.size()==maximum){return boundError();}
      names.emplace_back(std::wstring_view(entry->FileName,length/sizeof(wchar_t)));
      const auto next=entry->NextEntryOffset;
      if(!next){break;}
      if(next<header+length||next%alignof(FILE_ID_BOTH_DIR_INFO)!=0||next>data.size()-offset-header){return listingError();}
      offset+=next;
    }
  }
#else
  // A duplicated descriptor would share the lease's directory offset.
  StateDescriptor fd(::openat(directory,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  if(fd.get()==InvalidStateHandle){return listingError();}
  const auto close=[](DIR* entries){::closedir(entries);};
  std::unique_ptr<DIR,decltype(close)> entries(::fdopendir(fd.get()),close);
  if(!entries){return listingError();}
  static_cast<void>(fd.release());
  for(;;){
    errno=0;
    const auto* element=::readdir(entries.get());
    if(!element){
      if(errno){return listingError();}
      return names;
    }
    if(names.size()==maximum){return boundError();}
    names.emplace_back(element->d_name);
  }
#endif
}

} // namespace biglybtc::storage::detail
