// Private, Qt-free Java/PCRE common syntax gate. Execution uses separately
// bounded Qt matching; unsupported syntax is refused before any query DNS.
struct PatternSyntax {
  std::string_view text;std::size_t at=0,captures=0;unsigned depth=0;
  unsigned char escaped(){
    require(at<text.size(),E::Unsupported);const auto c=static_cast<unsigned char>(text[at++]);
    require(std::string_view("dDsSwWtnrf").find(static_cast<char>(c))!=std::string_view::npos||!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')),E::Unsupported);
    return c;
  }
  int classAtom(){
    require(at<text.size(),E::Unsupported);const auto c=static_cast<unsigned char>(text[at++]);
    if(c=='\\'){const auto e=escaped();if(std::string_view("dDsSwW").find(static_cast<char>(e))!=std::string_view::npos){return -1;}if(e=='n'){return '\n';}if(e=='r'){return '\r';}if(e=='t'){return '\t';}if(e=='f'){return '\f';}return e;}
    require(c!='['&&c!=']'&&c!='&',E::Unsupported);return c;
  }
  bool atom(){
    require(at<text.size(),E::Unsupported);const auto c=text[at++];
    if(c=='('){
      require(++depth<=32,E::Limit);if(at<text.size()&&text[at]=='?'){require(at+1<text.size()&&text[at+1]==':',E::Unsupported);at+=2;}else{require(++captures<=32,E::Limit);}
      const bool nullable=expression();require(at<text.size()&&text[at++]==')',E::Unsupported);--depth;return nullable;
    }
    if(c=='['){
      if(at<text.size()&&text[at]=='^'){++at;}bool any=false;
      while(at<text.size()&&text[at]!=']'){
        const auto first=classAtom();any=true;
        if(at+1<text.size()&&text[at]=='-'&&text[at+1]!=']'){++at;const auto last=classAtom();require(first>=0&&last>=first,E::Unsupported);}
      }require(any&&at<text.size()&&text[at++]==']',E::Unsupported);return false;
    }
    if(c=='\\'){escaped();return false;}
    require(std::string_view(")|*+?{}").find(c)==std::string_view::npos,E::Unsupported);
    return c=='^'||c=='$';
  }
  unsigned count(){
    const auto start=at;unsigned value=0;while(at<text.size()&&text[at]>='0'&&text[at]<='9'){require(value<=1000,E::Limit);value=value*10+static_cast<unsigned>(text[at++]-'0');}require(at>start&&value<=10000,E::Unsupported);return value;
  }
  bool sequence(){
    bool nullable=true;
    while(at<text.size()&&text[at]!=')'&&text[at]!='|'){
      const bool anchor=text[at]=='^'||text[at]=='$';bool empty=atom();
      if(at<text.size()&&std::string_view("*+?{").find(text[at])!=std::string_view::npos){
        require(!anchor,E::Unsupported);const auto quantifier=text[at++];unsigned minimum=1;
        if(quantifier=='*'||quantifier=='?'){minimum=0;}else if(quantifier=='{'){
          minimum=count();unsigned maximum=minimum;if(at<text.size()&&text[at]==','){++at;maximum=at<text.size()&&text[at]=='}'?10000:count();}
          require(at<text.size()&&text[at++]=='}'&&maximum>=minimum,E::Unsupported);
        }empty=empty||minimum==0;if(at<text.size()&&text[at]=='?'){++at;}require(at==text.size()||text[at]!='+',E::Unsupported);
      }nullable=nullable&&empty;
    }return nullable;
  }
  bool expression(){bool nullable=sequence();while(at<text.size()&&text[at]=='|'){++at;const bool branch=sequence();nullable=nullable||branch;}return nullable;}
};
void mappingSource(std::string_view value,bool capture,std::size_t groups){
  const auto key=[&](std::string_view text){require(!text.empty(),E::Unsupported);if(capture){std::size_t n=0;for(const auto c:text){require(c>='0'&&c<='9'&&n<=32,E::Unsupported);n=n*10+static_cast<unsigned>(c-'0');}require(n>0&&n<=groups,E::Unsupported);}};
  auto position=value.find("${");if(position==value.npos){key(value);return;}
  while(position!=value.npos){
    const auto end=value.find('}',position+2);require(end!=value.npos,E::Unsupported);auto token=value.substr(position+2,end-position-2);std::vector<std::string_view> args;
    for(std::size_t at=0;;){const auto comma=token.find(',',at);args.push_back(token.substr(at,comma==token.npos?token.size()-at:comma-at));if(comma==token.npos){break;}at=comma+1;}
    key(args[0]);if(args.size()>1){std::size_t argument=2;auto commands=args[1];for(std::size_t at=0;;){const auto plus=commands.find('+',at);const auto command=commands.substr(at,plus==commands.npos?commands.size()-at:plus-at);
      if(command=="replace"){require(argument+2<=args.size(),E::Unsupported);const auto syntax=inspectMetasearchPattern(args[argument++]);require(bool(syntax)&&!syntax->nullable,E::Unsupported);++argument;}
      else{require(command=="ucase"||command=="lcase"||command=="urldecode",E::Unsupported);}if(plus==commands.npos){break;}at=plus+1;
    }require(argument==args.size(),E::Unsupported);}position=value.find("${",end+1);
  }
}
