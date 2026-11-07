// No caller-supplied PCRE verbs, JIT or unbounded search. Every attempt is
// anchored and charged to the same document budget, including filters/replace.
void patternSubject(const QString& subject){for(const auto c:subject){require(c!=QChar(0x85)&&c!=QChar(0x2028)&&c!=QChar(0x2029),E::Unsupported);}}
struct BoundedPattern;
struct PatternBudget {
  std::size_t attempts=10000,scanned=8*1024*1024;
  // Per-document only: neither query lifetime nor another source can reuse it.
  std::map<std::pair<QString,int>,std::shared_ptr<const BoundedPattern>> compiled;
};
struct BoundedPattern {
  QRegularExpression expression;QString prefix;bool nullable=false;
  BoundedPattern(QString pattern,QRegularExpression::PatternOptions options={}):expression(
      QStringLiteral("(*NO_JIT)(*LIMIT_MATCH=10000)(*LIMIT_DEPTH=64)(*LIMIT_HEAP=1024)(*ANYCRLF)")+pattern,options){
    const auto syntax=inspectMetasearchPattern(pattern.toUtf8().toStdString());require(bool(syntax),E::Unsupported);nullable=syntax->nullable;
    require(expression.isValid()&&expression.captureCount()<=32,E::Unsupported);
    // A literal initial run is mandatory only when no alternation is present.
    if(!pattern.contains(u'|')){qsizetype at=pattern.startsWith(u'^')?1:0;
      while(at<pattern.size()&&prefix.size()<128&&!QStringView(u".[](){}*+?|^$\\").contains(pattern[at])){prefix+=pattern[at++];}
      if(at<pattern.size()&&QStringView(u"*+?{").contains(pattern[at])&&!prefix.isEmpty()){prefix.chop(1);}
    }
  }
  std::optional<QRegularExpressionMatch> find(const QString& subject,qsizetype from,PatternBudget& budget)const{
    require(subject.size()<=4*1024*1024&&from>=0,E::Limit);
    for(qsizetype at=from;at<=subject.size();){
      if(!prefix.isEmpty()){
        const auto next=subject.indexOf(prefix,at,expression.patternOptions().testFlag(QRegularExpression::CaseInsensitiveOption)?Qt::CaseInsensitive:Qt::CaseSensitive);const auto scanned=static_cast<std::size_t>((next<0?subject.size():next)-at);require(scanned<=budget.scanned,E::Limit);budget.scanned-=scanned;if(next<0){return {};}at=next;
      }
      require(budget.attempts>0,E::Limit);--budget.attempts;
      const auto match=expression.match(subject,at,QRegularExpression::NormalMatch,QRegularExpression::AnchorAtOffsetMatchOption);
      require(match.isValid(),E::Limit);if(match.hasMatch()){return match;}if(at==subject.size()){return {};}
      const auto step=subject[at].isHighSurrogate()&&at+1<subject.size()&&subject[at+1].isLowSurrogate()?2:1;
      require(static_cast<std::size_t>(step)<=budget.scanned,E::Limit);budget.scanned-=static_cast<std::size_t>(step);at+=step;
    }return {};
  }
};
const BoundedPattern& compiledPattern(PatternBudget& budget,const QString& pattern,QRegularExpression::PatternOptions options={}){
  const auto key=std::pair{pattern,static_cast<int>(options.toInt())};const auto found=budget.compiled.find(key);if(found!=budget.compiled.end()){return *found->second;}
  require(budget.compiled.size()<256,E::Limit);auto compiled=std::make_shared<const BoundedPattern>(pattern,options);const auto added=budget.compiled.emplace(key,std::move(compiled));return *added.first->second;
}
bool postFilter(const MetasearchMapping& mapping,const QString& content,std::string_view query,PatternBudget& budget){
  if(mapping.postFilter.empty()){return true;}require(ascii(content),E::Unsupported);
  auto pattern=QString::fromStdString(mapping.postFilter);
  if(pattern.contains(QStringLiteral("%s"))){const auto actual=QString::fromUtf8(query.data(),static_cast<qsizetype>(query.size()));require(ascii(actual),E::Unsupported);pattern.replace(QStringLiteral("%s"),QRegularExpression::escape(actual));}
  // This insertion intentionally corrects Original's replaceAll replacement
  // quoting bug: the actual query remains literal, including '$' and '\'.
  return compiledPattern(budget,pattern,QRegularExpression::CaseInsensitiveOption).find(content,0,budget).has_value();
}
QString mappedExpression(const std::string& source,const std::function<std::optional<QString>(QStringView)>& get,PatternBudget& budget){
  auto result=QString::fromStdString(source);qsizetype at=0;
  while((at=result.indexOf(QStringLiteral("${"),at))>=0){
    const auto end=result.indexOf(u'}',at+2);require(end>=0,E::Unsupported);const auto args=result.mid(at+2,end-at-2).split(u',',Qt::KeepEmptyParts);require(!args.isEmpty(),E::Unsupported);
    auto current=get(QStringView(args[0]));if(!current){at=end+1;continue;}auto value=*current;qsizetype argument=2;
    if(args.size()>1){for(const auto& command:args[1].split(u'+',Qt::KeepEmptyParts)){
      if(command==QStringLiteral("replace")){
        require(argument+2<=args.size(),E::Unsupported);const auto pattern=args[argument++],replacement=args[argument++];const auto& search=compiledPattern(budget,pattern);require(!search.nullable,E::Unsupported);patternSubject(value);QString changed;qsizetype position=0;
        while(const auto found=search.find(value,position,budget)){require(found->capturedEnd()>found->capturedStart(),E::Unsupported);changed+=value.mid(position,found->capturedStart()-position)+replacement;require(changed.size()<=65536,E::Limit);position=found->capturedEnd();}changed+=value.mid(position);value=std::move(changed);
      }else if(command==QStringLiteral("ucase")||command==QStringLiteral("lcase")){
        require(ascii(value),E::Unsupported);const bool upper=command==QStringLiteral("ucase");require(!value.contains(upper?u'i':u'I'),E::Unsupported);for(auto& c:value){if(upper&&c>=u'a'&&c<=u'z'){c=QChar(c.unicode()-32);}else if(!upper&&c>=u'A'&&c<=u'Z'){c=QChar(c.unicode()+32);}}
      }else if(command==QStringLiteral("urldecode")){
        auto bytes=value.toUtf8();QByteArray decoded;const auto hex=[](char c)->int{if(c>='0'&&c<='9'){return c-'0';}if(c>='a'&&c<='f'){return c-'a'+10;}if(c>='A'&&c<='F'){return c-'A'+10;}return -1;};
        for(qsizetype n=0;n<bytes.size();++n){if(bytes[n]=='+'){decoded+=' ';}else if(bytes[n]=='%'){require(n+2<bytes.size(),E::Unsupported);const auto a=hex(bytes[n+1]),b=hex(bytes[n+2]);require(a>=0&&b>=0,E::Unsupported);decoded+=static_cast<char>((a<<4)|b);n+=2;}else{decoded+=bytes[n];}}
        value=QString::fromUtf8(decoded);require(value.toUtf8()==decoded&&!value.contains(QChar{}),E::Unsupported);
      }else{throw Failure{E::Unsupported};}
      require(value.size()<=65536,E::Limit);
    }}
    // Literal insertion intentionally avoids Original replaceFirst's unsafe
    // replacement-string interpretation; captured '$'/'\' remain data.
    result.replace(at,end-at+1,value);at+=value.size();require(result.size()<=65536,E::Limit);
  }return result;
}
struct MappedValue {const MetasearchMapping* mapping;QString content;bool decimal=false;};
