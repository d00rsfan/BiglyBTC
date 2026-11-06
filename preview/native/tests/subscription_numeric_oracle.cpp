// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_numeric_filter.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace search=biglybtc::search;
constexpr qint64 InputLimit=1024*1024;
constexpr char ObservationsHash[]="f452f54bc059efd97a6e3dded4457a3b8f92893c7386829dee563f1ab4ae924a";

void require(bool condition,const char* message){
  if(!condition){throw std::runtime_error(message);}
}
std::int64_t integer(const QJsonValue& value){
  require(value.isDouble(),"Expected an actual JSON integer");
  // QJsonValue retains integer tokens without converting Long extrema through
  // double. Different default sentinels also reject fractional/out-of-range
  // values rather than silently accepting a default as an observed integer.
  const auto low=value.toInteger(std::numeric_limits<qint64>::min());
  const auto high=value.toInteger(std::numeric_limits<qint64>::max());
  require(low==high,"Nonintegral or out-of-range observed integer");
  return low;
}
std::int32_t count(const QJsonValue& value){
  const auto number=integer(value);
  require(number>=std::numeric_limits<std::int32_t>::min()
      &&number<=std::numeric_limits<std::int32_t>::max(),"Observed getter exceeds int32");
  return static_cast<std::int32_t>(number);
}
bool boolean(const QJsonValue& value){
  require(value.isBool(),"Expected an actual JSON boolean");return value.toBool();
}
QJsonObject thresholds(const search::SubscriptionNumericFilter& value){
  return {{"min_size",QJsonValue(static_cast<qint64>(value.minSize))},
          {"max_size",QJsonValue(static_cast<qint64>(value.maxSize))},
          {"min_seeds",QJsonValue(static_cast<qint64>(value.minSeeds))},
          {"max_seeds",QJsonValue(static_cast<qint64>(value.maxSeeds))},
          {"min_peers",QJsonValue(static_cast<qint64>(value.minPeers))}};
}
void compareThresholds(const search::SubscriptionNumericFilter& actual,const QJsonObject& original){
  require(actual.minSize==integer(original.value("min_size")),"min_size differs from Original getter");
  require(actual.maxSize==integer(original.value("max_size")),"max_size differs from Original getter");
  require(actual.minSeeds==integer(original.value("min_seeds")),"min_seeds differs from Original getter");
  require(actual.maxSeeds==integer(original.value("max_seeds")),"max_seeds differs from Original getter");
  require(actual.minPeers==integer(original.value("min_peers")),"min_peers differs from Original getter");
}
}

int main(int argc,char** argv){
  QString currentLabel;
  try{
    require(argc==3&&std::string_view(argv[1])=="--original-values",
        "Closed arguments: --original-values OBSERVATIONS_JSON");
    const auto path=QString::fromLocal8Bit(argv[2]);const QFileInfo info(path);
    require(info.isFile()&&!info.isSymLink(),"Observation input must be a regular non-symlink file");
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot read actual Original observations");
    require(file.size()>0&&file.size()<=InputLimit,"Original observations exceed input bound");
    const auto bytes=file.read(InputLimit+1);
    require(file.error()==QFileDevice::NoError&&file.atEnd()&&bytes.size()<=InputLimit,
        "Observation read failed or exceeded input bound");
    require(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==ObservationsHash,
        "Only the pinned actual Original observation bytes are admitted");
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
    require(error.error==QJsonParseError::NoError&&document.isObject(),"Invalid Original observation document");
    const auto root=document.object();
    require(root.value("schema").toString()=="BIGLYBT_PUBLIC_NUMERIC_FILTER_VALUE_OBSERVATIONS_V1"
        &&integer(root.value("case_count"))==58&&root.value("cases").isArray(),"Wrong actual observation schema");
    const auto cases=root.value("cases").toArray();require(cases.size()==58,"Expected exactly 58 actual cases");
    QSet<QString> labels;QJsonArray nativeRows;int comparable=0,unsupported=0;
    for(const auto& entry:cases){
      require(entry.isObject(),"Invalid actual case row");const auto row=entry.toObject();
      require(row.value("label").isString()&&row.value("filters_json").isString(),"Missing actual case identity/JSON");
      const auto label=row.value("label").toString();currentLabel=label;require(!label.isEmpty()&&!labels.contains(label),"Duplicate actual case label");labels.insert(label);
      const auto raw=row.value("filters_json").toString().toUtf8().toStdString();
      const bool excluded=boolean(row.value("native_refusal_original_observation_only"));
      auto parsed=search::parseSubscriptionNumericFilter(raw);
      if(excluded){
        // Original swallowed-constructor observations are deliberately outside
        // the supported parity domain. The actual parser calls malformed
        // decimal input Malformed; do not relabel its production error enum.
        require(!parsed&&parsed.error()==search::SubscriptionValueError::Malformed,
            "An excluded malformed Original constructor case was not refused");
        nativeRows.append(QJsonObject{{"label",label},{"classification","unsupported_original_observation_only"},
            {"native_error","Malformed"}});++unsupported;continue;
      }
      require(bool(parsed),"Supported Original numeric input was refused");
      require(parsed->originalJson&&*parsed->originalJson==raw,"Actual filter JSON was not retained exactly");
      require(row.value("loaded_thresholds").isObject()&&row.value("input").isObject(),"Missing original getter values");
      compareThresholds(*parsed,row.value("loaded_thresholds").toObject());
      const auto input=row.value("input").toObject();const auto name=input.value("name");
      require(name.isNull()||name.isString(),"Invalid explicit Original name value");
      const search::SubscriptionNumericResult value{!name.isNull(),integer(input.value("size")),
          count(input.value("seeds")),count(input.value("peers"))};
      const bool active=parsed->active(),filtered=search::isSubscriptionNumericFiltered(*parsed,value);
      require(active==boolean(row.value("active")),"Active differs from Original public value");
      require(filtered==boolean(row.value("filtered")),"Predicate differs from Original public value");
      nativeRows.append(QJsonObject{{"label",label},{"classification","compared_original_public_values"},
          {"loaded_thresholds",thresholds(*parsed)},{"active",active},{"filtered",filtered}});++comparable;
    }
    require(comparable==50&&unsupported==8,"Wrong comparable/refusal partition");
    const QJsonObject result{{"schema","BIGLYBTC_NATIVE_NUMERIC_FILTER_ORIGINAL_VALUE_COMPARISON_V1"},
        {"status","PASS_NATIVE_SUBSCRIPTION_NUMERIC_ORIGINAL_VALUES"},
        {"Original_commit","cb8c6cb9bb97916cbc7e80338a017200ffc3e71f"},
        {"observations_sha256",QString::fromLatin1(ObservationsHash)},
        {"case_count",58},{"compared",comparable},{"unsupported_original_observations",unsupported},
        {"scope","Explicit public getter values only; no phase projection or acquisition permission"},
        {"cases",nativeRows}};
    std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString()<<'\n'
        <<"PASS_NATIVE_SUBSCRIPTION_NUMERIC_ORIGINAL_VALUES_50_PLUS_8\n";
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"FAIL_NATIVE_SUBSCRIPTION_NUMERIC_ORIGINAL_VALUES case="
        <<currentLabel.toUtf8().toStdString()<<": "<<error.what()<<'\n';return 1;
  }
}
