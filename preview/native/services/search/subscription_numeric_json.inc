// Reused private bounded JSON envelope from subscription_values.cpp (typed
// QString correction retained). No file/profile/network/effect operations.
struct Json {
  std::string raw;QJsonObject value;
  std::map<std::string,std::pair<std::size_t,std::size_t>> fields,members;
  static std::expected<Json,V> parse(std::string raw){
    if(raw.size()>Limits.inputBytes){return std::unexpected(V::Limit);}QStringDecoder decoder(QStringDecoder::Utf8);const QString text=decoder.decode(QByteArray::fromStdString(raw));
    if(decoder.hasError()||text.contains(QChar(0))){return std::unexpected(V::Malformed);}
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(raw),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject()){return std::unexpected(V::Malformed);}
    Json out;out.raw=std::move(raw);out.value=doc.object();std::size_t at=0,nodes=0;
    const auto whitespace=[&]{while(at<out.raw.size()&&(out.raw[at]==' '||out.raw[at]=='\n'||out.raw[at]=='\r'||out.raw[at]=='\t')){++at;}};
    const auto string=[&]()->std::string_view{const auto begin=at++;while(at<out.raw.size()){if(out.raw[at]=='\\'){at+=2;}else if(out.raw[at++]=='"'){return std::string_view(out.raw).substr(begin,at-begin);}}return {};};
    const auto walk=[&](auto&& self,unsigned depth)->bool{
      whitespace();if(depth>64||++nodes>100000||at>=out.raw.size()){return false;}
      if(out.raw[at]=='{'){
        ++at;whitespace();std::set<QString> keys;if(at<out.raw.size()&&out.raw[at]=='}'){++at;return true;}
        for(;;){whitespace();const auto memberBegin=at;if(at>=out.raw.size()||out.raw[at]!='"'){return false;}const auto token=string();if(token.empty()){return false;}
          const auto keyDoc=QJsonDocument::fromJson("["+QByteArray(token.data(),static_cast<qsizetype>(token.size()))+"]");if(keyDoc.isNull()||!keyDoc.isArray()){return false;}const auto key=keyDoc.array()[0].toString();if(!keys.insert(key).second){return false;}
          whitespace();if(at>=out.raw.size()||out.raw[at++]!=':'){return false;}whitespace();const auto begin=at;if(!self(self,depth+1)){return false;}if(depth==0){out.fields.emplace(key.toUtf8().toStdString(),std::pair{begin,at});out.members.emplace(key.toUtf8().toStdString(),std::pair{memberBegin,at});}whitespace();
          if(at>=out.raw.size()){return false;}const auto end=out.raw[at++];if(end=='}'){return true;}if(end!=','){return false;}
        }
      }
      if(out.raw[at]=='['){++at;whitespace();if(at<out.raw.size()&&out.raw[at]==']'){++at;return true;}for(;;){if(!self(self,depth+1)){return false;}whitespace();if(at>=out.raw.size()){return false;}const auto c=out.raw[at++];if(c==']'){return true;}if(c!=','){return false;}}}
      if(out.raw[at]=='"'){return !string().empty();}
      const auto begin=at;while(at<out.raw.size()&&out.raw[at]!=','&&out.raw[at]!='}'&&out.raw[at]!=']'&&out.raw[at]!=' '&&out.raw[at]!='\n'&&out.raw[at]!='\t'&&out.raw[at]!='\r'){++at;}return at>begin;
    };
    if(!walk(walk,0)){return std::unexpected(V::Ambiguous);}whitespace();if(at!=out.raw.size()){return std::unexpected(V::Malformed);}return out;
  }
  std::string slice(std::string_view key)const{const auto p=fields.find(std::string(key));return p==fields.end()?std::string{}:raw.substr(p->second.first,p->second.second-p->second.first);}
};
