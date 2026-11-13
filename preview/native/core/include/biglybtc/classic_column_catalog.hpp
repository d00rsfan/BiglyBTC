#pragma once
#include <array>
#include <string_view>
namespace biglybtc::core {
struct ClassicColumn { std::string_view nativeId,originalId,label;int width; };
inline constexpr std::array<ClassicColumn,34> ClassicColumnCatalog{{
  {"name","name","Name",300},
  {"state","status","State",120},
  {"size","sizewithdnd","Total Size",100},
  {"progress","done","Done",80},
  {"download-rate","downspeed","Down Speed",90},
  {"upload-rate","upspeed","Up Speed",90},
  {"received","down","Downloaded",100},
  {"sent","up","Sent",100},
  {"hash","torrenthash","Hash",320},
  {"saved-queue-position","#","Order",64},
  {"saved-selected-size","size","Size",100},
  {"saved-selected-remaining","remaining","Remaining",100},
  {"saved-selected-done","","Selected Done",100},
  {"save-path","savepath","Save Path",300},
  {"tracker-name","trackername","Tracker Name",120},
  {"share-ratio","shareRatio","Ratio",80},
  {"tracker-status","tracker","Tracker Status",240},
  {"tags","tags","Tags",180},
  {"seeds","seeds","Seeds",70},
  {"peers","peers","Peers",70},
  {"category","category","Category",100},
  {"date-added","date_added","Date Added",150},
  {"date-completed","DateCompleted","Date Completed",150},
  {"upload-limit","maxupspeed","Upload Limit",90},
  {"eta","eta","ETA",90},
  {"time-since-download","timesincedownload","Since Download",100},
  {"time-since-upload","timesinceupload","Since Upload",100},
  {"merged-data","mergeddata","Merged Data",100},
  {"file-count","filecount","Files",70},
  {"comment","commenticon","Comment",160},
  {"seeding-rank","SeedingRank","Seeding Rank",90},
  {"chat-pending","azbuddy.ui.column.msgpending","Chat Messages",60},
  {"subscription","azsubs.ui.column.subs","Subscriptions",60},
  {"rating","RatingColumn","Rating",95},
}};
}
