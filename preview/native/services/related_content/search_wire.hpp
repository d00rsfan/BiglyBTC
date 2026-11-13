// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "search_bloom.hpp"
namespace biglybtc::services::related_content {
enum class SearchOperation {Query,FetchBloom,UpdateBloom};
struct SearchMessage {SearchOperation operation=SearchOperation::Query;SearchRequest query;std::int32_t bloomCount=0;};
struct SearchContact {std::string address;std::uint16_t port=0;bool operator==(const SearchContact&)const=default;};
struct SearchResponse {bool resultsPresent=false;std::vector<Item> items;std::vector<SearchContact> contacts;std::optional<SearchBloom> bloom;std::optional<std::int32_t> unchangedCount;};
std::string searchHandler(bool bigly);
Result<std::string> encodeSearchRequest(const SearchMessage&);
Result<SearchMessage> decodeSearchRequest(std::string);
Result<std::string> encodeSearchResponse(const SearchResponse&);
Result<SearchResponse> decodeSearchResponse(std::string);
}
