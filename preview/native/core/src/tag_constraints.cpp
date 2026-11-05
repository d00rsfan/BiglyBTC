// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/tag_constraints.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
namespace biglybtc::core::tags::constraints {
  namespace {
    struct Rejected {
      Problem problem;
    };
    [[noreturn]] void reject(ProblemKind kind,const char* message){
      throw Rejected{
        {
          kind,message
        }
      };
    }
    void need(bool ok,const char* message){
      if(!ok){
        reject(ProblemKind::Invalid,message);
      }
    }
    bool quote(char16_t c){
      return c==u'"'||c==u'\u201c'||c==u'\u201d';
    }
    bool white(char16_t c){
      return (c>=9&&c<=13)||(c>=28&&c<=32)||c==0x1680||(c>=0x2000&&c<=0x2006)||(c>=0x2008&&c<=0x200a)||c==0x2028||c==0x2029||c==0x205f||c==0x3000;
    }
    Text trim(Text t){
      // Java String.trim uses <=U+0020 (not Character.isWhitespace).
      std::size_t a=0,b=t.size();
      while(a<b&&t[a]<=32){
        ++a;
      }
      while(b>a&&t[b-1]<=32){
        --b;
      }
      return t.substr(a,b-a);
    }
    std::string ascii(std::u16string_view t){
      std::string out;
      out.reserve(t.size());
      for(const auto c:t){
        if(c>127){
          reject(ProblemKind::Unavailable,"Java Unicode/numeric provider required");
        }
        out.push_back(static_cast<char>(c));
      }
      return out;
    }
    std::string lower(std::string t){
      for(auto& c:t){
        if(c>='A'&&c<='Z'){
          c=static_cast<char>(c+32);
        }
      }
      return t;
    }
    Text literal(const Text& t){
      need(t.size()>=2&&quote(t.front())&&quote(t.back()),"Quoted string literal required");
      Text out=t.substr(1,t.size()-2);
      for(std::size_t p=0;(p=out.find(u"\\\"",p))!=Text::npos;){
        out.replace(p,2,u"\"");
        ++p;
      }
      return out;
    }
    bool quoted(const Text& t){
      return t.size()>=2&&quote(t.front())&&quote(t.back());
    }
    #include "tag_constraint_catalog.inc"
    std::string canonical(std::string name,bool function){
      name=lower(std::move(name));
      if(function){
        for(const auto& [a,c]:Functions){
          if(a==name){
            return std::string(c);
          }
        }
      }
      else{
        for(const auto& [a,c]:Keywords){
          if(a==name){
            return std::string(c);
          }
        }
      }
      return {
      };
    }
    bool oneOf(std::string_view s,std::initializer_list<std::string_view> values){
      return std::find(values.begin(),values.end(),s)!=values.end();
    }
    std::int64_t integer(std::string_view s,int base=10){
      std::int64_t n=0;
      if(s.starts_with('+')){
        s.remove_prefix(1);
      }
      auto r=std::from_chars(s.data(),s.data()+s.size(),n,base);
      need(!s.empty()&&r.ec==std::errc{
      }
      &&r.ptr==s.data()+s.size(),"Invalid Java long literal");
      return n;
    }
    double decimal(std::string s){
      bool negative=false;
      if(!s.empty()&&(s.front()=='+'||s.front()=='-')){
        negative=s.front()=='-';
        s.erase(0,1);
      }
      if(s=="NaN"){
        return std::numeric_limits<double>::quiet_NaN();
      }
      if(s=="Infinity"){
        return negative?-INFINITY:INFINITY;
      }
      if(!s.empty()&&oneOf(std::string_view(s).substr(s.size()-1),{
        "d","D","f","F"
      })){
        s.pop_back();
      }
      const bool hex=s.starts_with("0x")||s.starts_with("0X");
      if(hex){
        s.erase(0,2);
      }
      std::size_t p=0,digits=0;
      bool dot=false;
      const auto digit=[&](char c){
        return (c>='0'&&c<='9')||(hex&&((c>='a'&&c<='f')||(c>='A'&&c<='F')));
      };
      for(;p<s.size();++p){
        if(digit(s[p])){
          ++digits;
        }
        else if(s[p]=='.'&&!dot){
          dot=true;
        }
        else{
          break;
        }
      }
      need(digits!=0,"Invalid Java double literal");
      if(p<s.size()&&oneOf(std::string_view(s).substr(p,1),hex?std::initializer_list<std::string_view>{
        "p","P"
      }
      :std::initializer_list<std::string_view>{
        "e","E"
      })){
        ++p;
        if(p<s.size()&&(s[p]=='+'||s[p]=='-')){
          ++p;
        }
        const auto a=p;
        while(p<s.size()&&s[p]>='0'&&s[p]<='9'){
          ++p;
        }
        need(a!=p,"Invalid double exponent");
      }
      else{
        need(!hex,"Hex double requires binary exponent");
      }
      need(p==s.size(),"Invalid Java double literal");
      double out=0;
      const auto r=std::from_chars(s.data(),s.data()+s.size(),out,hex?std::chars_format::hex:std::chars_format::general);
      if(r.ec==std::errc::result_out_of_range){
        reject(ProblemKind::Unavailable,"Extreme Java double rounding provider required");
      }
      need(r.ec==std::errc{
      }
      &&r.ptr==s.data()+s.size(),"Invalid Java double literal");
      return negative?-out:out;
    }
    double number(const Value& v){
      if(auto p=std::get_if<std::int64_t>(&v)){
        return static_cast<double>(*p);
      }
      if(auto p=std::get_if<float>(&v)){
        return *p;
      }
      if(auto p=std::get_if<double>(&v)){
        return *p;
      }
      reject(ProblemKind::Invalid,"Numeric value required");
    }
    std::int64_t longValue(const Value& v){
      if(auto p=std::get_if<std::int64_t>(&v)){
        return *p;
      }
      const double d=number(v);
      if(std::isnan(d)){
        return 0;
      }
      if(d>=static_cast<double>(INT64_MAX)){
        return INT64_MAX;
      }
      if(d<=static_cast<double>(INT64_MIN)){
        return INT64_MIN;
      }
      return static_cast<std::int64_t>(d);
    }
    std::int32_t intValue(const Value& v){
      if(auto p=std::get_if<std::int64_t>(&v)){
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*p));
      }
      const double d=number(v);
      if(std::isnan(d)){
        return 0;
      }
      if(d>=INT32_MAX){
        return INT32_MAX;
      }
      if(d<=INT32_MIN){
        return INT32_MIN;
      }
      return static_cast<std::int32_t>(d);
    }
    std::int64_t wrap(std::uint64_t v){
      return std::bit_cast<std::int64_t>(v);
    }
    struct Node;
    using Ptr=std::shared_ptr<const Node>;
    struct Node {
      enum class Kind {
        Raw,True,False,Or,And,Xor,Not,Call,Params
      };
      Kind kind;
      Text raw;
      std::string function;
      std::vector<Ptr> args;
    };
    struct Reference {
      Ptr expression;
      std::optional<Text> params;
    };
    struct Parser {
      Limits limits;
      std::size_t count=0,depth=0;
      std::map<Text,Reference> context;
      std::vector<Text> requiredTags;bool requiresJavaPattern=false;
      struct Depth {
        Parser& p;
        explicit Depth(Parser& v):p(v){
          if(++p.depth>p.limits.depth){
            reject(ProblemKind::Bounds,"Constraint parse depth exceeded");
          }
        }
        ~Depth(){
          --p.depth;
        }
      };
      Ptr make(Node::Kind kind,Text raw={
      },std::string function={
      },std::vector<Ptr> args={
      }){
        if(++count>limits.nodes){
          reject(ProblemKind::Bounds,"Constraint AST bound exceeded");
        }
        return std::make_shared<const Node>(Node{
          kind,std::move(raw),std::move(function),std::move(args)
        });
      }
      static std::optional<std::pair<std::size_t,Text>> operation(const Text& t){
        for(std::size_t p=1;p+1<t.size();++p){
          for(const Text op:{
            u"==",u"!=",u">=",u">",u"<=",u"<",u"+",u"-",u"*",u"/",u"%"
          }){
            if(t.compare(p,op.size(),op)==0&&p+op.size()<t.size()){
              return std::pair{
                p,op
              };
            }
          }
        }
        return {
        };
      }
      Text comments(Text text){
        for(std::size_t a=0;(a=text.find(u"/*",a))!=Text::npos;){
          const auto b=text.find(u"*/",a+2);
          if(b==Text::npos){
            break;
          }
          text.erase(a,b+2-a);
        }
        Text out;
        std::size_t a=0;
        while(a<=text.size()){
          const auto b=text.find(u'\n',a);
          auto line=trim(text.substr(a,b==Text::npos?Text::npos:b-a));
          if(!line.empty()&&line.front()==u'#'){
            line.clear();
          }
          const auto slash=line.find(u"//");
          if(slash!=Text::npos){
            line.erase(slash);
          }
          if(!line.empty()){
            out+=line;
            out+=u'\n';
          }
          if(b==Text::npos){
            break;
          }
          a=b+1;
        }
        return trim(out);
      }
      Text key(){
        const auto n=std::to_string(context.size());
        return u"{"+Text(n.begin(),n.end())+u"}";
      }
      Ptr start(Text text){
        Depth guard(*this);
        text=comments(std::move(text));
        const auto word=lower(asciiBoolean(text));
        if(text.empty()||word=="false"){
          return make(Node::Kind::False);
        }
        if(word=="true"){
          return make(Node::Kind::True);
        }
        bool inQuote=false,escaped=false;
        int level=0;
        std::size_t begin=0;
        Text out;
        for(std::size_t i=0;i<text.size();++i){
          const auto c=text[i];
          const bool esc=c==u'\\';
          if(esc&&escaped){
            escaped=false;
            continue;
          }
          if(quote(c)&&!escaped){
            inQuote=!inQuote;
          }
          escaped=esc;
          if(!inQuote){
            if(c==u'('){
              if(++level==1){
                begin=i+1;
              }
            }
            else if(c==u')'){
              if(--level==0){
                auto body=trim(text.substr(begin,i-begin));
                const bool call=!out.empty()&&((out.back()>=u'a'&&out.back()<=u'z')||(out.back()>=u'A'&&out.back()<=u'Z')||(out.back()>=u'0'&&out.back()<=u'9'));
                if(call){
                  const auto k=key();
                  preparse(body);
                  context[k]=Reference{
                    {
                    },std::move(body)
                  };
                  out+=u"("+k+u")";
                }
                else{
                  auto sub=start(std::move(body));
                  const auto k=key();
                  context[k]=Reference{
                    std::move(sub),{
                    }
                  };
                  out+=k;
                }
              }
            }
            else if(level==0&&!white(c)){
              out+=c;
            }
          }
          else if(level==0){
            out+=c;
          }
        }
        need(!inQuote&&level==0,"Unmatched constraint quote or parenthesis");
        return basic(out);
      }
      static std::string asciiBoolean(const Text& t){
        std::string out;
        for(auto c:t){
          if(c>127){
            return {
            };
          }
          out+=static_cast<char>(c);
        }
        return out;
      }
      Ptr dereference(const Text& key){
        Depth guard(*this);
        auto it=context.find(key);
        need(it!=context.end(),"Unknown constraint placeholder");
        if(it->second.params){
          return make(Node::Kind::Params,{
          }, {
          },arguments(*it->second.params));
        }
        return it->second.expression;
      }
      Ptr argument(Text t){
        t=trim(std::move(t));
        if(!t.empty()&&quote(t.front())){
          return make(Node::Kind::Raw,std::move(t));
        }
        if(t.starts_with(u"{")){
          return dereference(t);
        }
        if(t.find(u'(')!=Text::npos||operation(t)){
          return start(std::move(t));
        }
        return make(Node::Kind::Raw,std::move(t));
      }
      std::vector<Ptr> arguments(Text t){
        Depth guard(*this);
        t=trim(std::move(t));
        if(t.empty()){
          return {
          };
        }
        if(t.find(u',')==Text::npos){
          auto arg=argument(std::move(t));
          return arg->kind==Node::Kind::Params?arg->args:std::vector<Ptr>{
            arg
          };
        }
        bool quoted=false;
        int level=0;
        Text current;
        std::vector<Ptr> args;
        for(std::size_t i=0;i<t.size();++i){
          const auto c=t[i];
          if(quote(c)&&(i==0||t[i-1]!=u'\\')){
            quoted=!quoted;
          }
          if(c==u','&&!quoted&&level==0){
            args.push_back(argument(current));
            current.clear();
          }
          else{
            if(!quoted){
              if(c==u'('){
                ++level;
              }
              else if(c==u')'){
                --level;
              }
            }
            if(quoted||!white(c)){
              current+=c;
            }
          }
          if(args.size()>limits.arguments){
            reject(ProblemKind::Bounds,"Constraint argument bound exceeded");
          }
        }
        args.push_back(argument(current));
        if(args.size()>limits.arguments){
          reject(ProblemKind::Bounds,"Constraint argument bound exceeded");
        }
        return args;
      }
      static bool stringLiteral(const Ptr& p){
        return p->kind==Node::Kind::Raw&&quoted(p->raw);
      }
      static Value numericLiteral(const Ptr& p){
        need(p->kind==Node::Kind::Raw,"Numeric literal required");
        auto s=ascii(p->raw);
        if(s.starts_with("0x")){
          return integer(std::string_view(s).substr(2),16);
        }
        if(s.starts_with('#')){
          return integer(std::string_view(s).substr(1),16);
        }
        return decimal(s);
      }
      void validate(std::string_view f,const std::vector<Ptr>& a){
        const auto n=a.size();
        const auto lit=[&](std::size_t i){
          return i<n&&stringLiteral(a[i]);
        };
        if(oneOf(f,{
          "hastag","hastagage","counttag","hastaggroup","getconfig","javascript","hasnet","trackerpeers","trackerseeds"
        })){
          need(n==1&&lit(0),"One string literal required");
        }
        else if(f=="tagposition"){
          need((n==1||n==2)&&lit(0),"Tag position arguments invalid");
          if(n==2){
            (void)numericLiteral(a[1]);
          }
        }
        else if(f=="pluginoption"){
          need(n==2&&lit(0)&&lit(1),"Plugin option requires two literals");
        }
        else if(oneOf(f,{
          "gettagweight","gettagsort"
        })){
          need(n<=2,"Tag options require at most two literals");
          for(const auto& p:a){
            need(stringLiteral(p),"String literal required");
          }
          if(n){
            auto opt=literal(a.back()->raw);
            if(f=="gettagsort"&&n==1){
              opt.clear();
            }
            opt=trim(opt);
            if(!opt.empty()){
              const auto s=lower(ascii(opt));
              need(oneOf(s,{
                "type=max","type=min","type=cumulative"
              }),"Invalid original tag aggregation options");
            }
          }
        }
        else if(f=="setcolours"){
          need(n>=1&&n<=3,"One to three colours required");
          for(const auto& p:a){
            (void)numericLiteral(p);
          }
        }
        else if(f=="settagsort"){
          need(n==1||n==2,"Invalid tag sort arguments");
          if(n==2||lit(0)){
            const auto i=n-1;
            need(lit(i),"Tag sort option literal required");
            const auto s=literal(a[i]->raw);
            need(s==u"r"||s==u"reverse"||s==u"random","Invalid tag sort option");
          }
        }
        else if(oneOf(f,{
          "isge","isgt","isle","islt","iseq","isneq","plus","minus","mult","div","rem","min","max"
        })){
          need(n==2,"Two numeric arguments required");
        }
        else if(oneOf(f,{
          "contains","matches"
        })){
          need(n==2||n==3,"Two or three arguments required");
          if(f=="matches"){
            need(lit(1),"Regex pattern literal required");
          }
          if(n==3){
            (void)numericLiteral(a[2]);
          }
        }
        else if(oneOf(f,{
          "length","count","lowercase","hourstoseconds","daystoseconds","weekstoseconds","timetoelapsed","tomb","tomib","togb","togib"
        })){
          need(n==1,"One argument required");
        }
        else if(f=="ifthenelse"){
          need(n==3,"Three conditional arguments required");
        }
        else{
          need(n==0,"No arguments permitted");
        }
        if(f=="hasnet"){
          need(oneOf(lower(ascii(literal(a[0]->raw))),{
            "public","i2p","tor"
          }),"Unknown original network literal");
        }
        if(f=="getconfig"){
          need(lower(ascii(literal(a[0]->raw)))=="queue.seeding.ignore.share.ratio","Unsupported original configuration key");
        }
      }
      void preparse(const Text& text){
        try{
          (void)arguments(text);
        }
        catch(const Rejected& error){
          if(error.problem.kind==ProblemKind::Bounds){
            throw;
          }
        }
      }
      Ptr callText(std::string f,const Text& text){
        preparse(text);
        return call(std::move(f),arguments(text));
      }
      Ptr call(std::string f,std::vector<Ptr> args){
        f=canonical(std::move(f),true);need(!f.empty(),"Unsupported original constraint function");validate(f,args);
        // The original constructor resolves these names even in an unselected
        // branch. Missing compile-time providers must not be short-circuited.
        if(oneOf(f,{"hastag","hastagage","counttag","tagposition"})){
          const auto name=literal(args[0]->raw);if(std::find(requiredTags.begin(),requiredTags.end(),name)==requiredTags.end()){requiredTags.push_back(name);}
        }else if((f=="gettagweight"&&args.size()==2)||(f=="gettagsort"&&!args.empty())){
          const auto names=literal(args[0]->raw);std::size_t p=0;
          for(;;){const auto end=names.find(u',',p);auto name=trim(names.substr(p,end==Text::npos?Text::npos:end-p));if(!name.empty()&&std::find(requiredTags.begin(),requiredTags.end(),name)==requiredTags.end()){requiredTags.push_back(std::move(name));}if(end==Text::npos){break;}p=end+1;}
        }
        if(f=="matches"){requiresJavaPattern=true;}
        return make(Node::Kind::Call,{},std::move(f),std::move(args));
      }
      Ptr basic(Text t){
        Depth guard(*this);
        for(const auto& [op,kind]:{
          std::pair{
            Text(u"||"),Node::Kind::Or
          },std::pair{
            Text(u"&&"),Node::Kind::And
          },std::pair{
            Text(u"^"),Node::Kind::Xor
          }
        }){
          if(t.find(op)==Text::npos){
            continue;
          }
          std::vector<Text> pieces;
          std::size_t a=0;
          for(;;){
            const auto b=t.find(op,a);
            pieces.push_back(t.substr(a,b==Text::npos?Text::npos:b-a));
            if(b==Text::npos){
              break;
            }
            a=b+op.size();
          }
          while(!pieces.empty()&&pieces.back().empty()){
            pieces.pop_back();
          }
          std::vector<Ptr> args;
          for(auto& p:pieces){
            args.push_back(basic(trim(std::move(p))));
          }
          need(kind!=Node::Kind::Xor||args.size()>=2,"XOR needs at least two operands");
          return make(kind,{
          }, {
          },std::move(args));
        }
        const auto q=t.find(u'?',1),c=t.rfind(u':');
        if(q!=Text::npos&&c!=Text::npos&&c>q+1&&c+1<t.size()){
          return callText("ifthenelse",t.substr(0,q)+u","+t.substr(q+1,c-q-1)+u","+t.substr(c+1));
        }
        if(const auto op=operation(t)){
          static constexpr std::pair<std::u16string_view,const char*> map[]={
            {
              u"==","iseq"
            },{
              u"!=","isneq"
            },{
              u">=","isge"
            },{
              u"<=","isle"
            },{
              u">","isgt"
            },{
              u"<","islt"
            },{
              u"+","plus"
            },{
              u"-","minus"
            },{
              u"*","mult"
            },{
              u"/","div"
            },{
              u"%","rem"
            }
          };
          for(const auto& [text,name]:map){
            if(op->second==text){
              return callText(name,t.substr(0,op->first)+u","+t.substr(op->first+text.size()));
            }
          }
        }
        if(t.starts_with(u"!")){
          return make(Node::Kind::Not,{
          }, {
          },{
            basic(trim(t.substr(1)))
          });
        }
        if(t.starts_with(u"{")){
          return dereference(t);
        }
        const auto p=t.find(u'(');
        if(p!=Text::npos&&p>0&&t.ends_with(u")")){
          auto arg=dereference(trim(t.substr(p+1,t.size()-p-2)));
          need(arg->kind==Node::Kind::Params,"Function parameter reference required");
          return call(ascii(t.substr(0,p)),arg->args);
        }
        reject(ProblemKind::Invalid,"Unsupported original constraint construct");
      }
    };
  }
  struct Expression::Impl {
    Ptr root;
    Limits limits;
    std::vector<Text> requiredTags;bool requiresJavaPattern=false;
  };
  namespace {
    struct Evaluator {
      const Facts& facts;
      Limits limits;
      std::size_t steps=0,work=0;
      void charge(std::size_t n){
        if(n>limits.workUnits-work){
          reject(ProblemKind::Bounds,"Constraint string work bound exceeded");
        }
        work+=n;
      }
      Value fact(std::string key){
        for(const auto& value:facts.values){
          if(value.key==key){
            if(auto t=std::get_if<Text>(&value.value)){
              charge(t->size());
            }
            if(auto list=std::get_if<Strings>(&value.value)){
              for(const auto& t:*list){
                charge(t.size());
              }
            }
            return value.value;
          }
        }
        throw Rejected{
          {
            ProblemKind::Unavailable,"Missing owned provider: "+key
          }
        };
      }
      Value keyword(const Text& name){
        const auto key=canonical(ascii(name),false);
        need(!key.empty(),"Invalid original constraint keyword");
        if(key=="max32"){
          return std::int64_t{
            INT32_MAX
          };
        }
        if(key=="min32"){
          return std::int64_t{
            INT32_MIN
          };
        }
        if(key=="max64"){
          return std::int64_t{
            INT64_MAX
          };
        }
        if(key=="min64"){
          return std::int64_t{
            INT64_MIN
          };
        }
        return fact(key);
      }
      Value numeric(const Ptr& arg){
        if(arg->kind!=Node::Kind::Raw){
          auto value=eval(arg);
          if(auto b=std::get_if<bool>(&value)){
            return std::int64_t(*b);
          }
          if(auto s=std::get_if<Text>(&value)){
            return integer(ascii(*s));
          }
          number(value);
          return value;
        }
        Text text=arg->raw;
        if(text==u"\u221e"){
          return std::int64_t{
            INT32_MAX
          };
        }
        if(!text.empty()&&((text[0]>=u'0'&&text[0]<=u'9')||(text.size()>1&&text[0]==u'-'&&text[1]>=u'0'&&text[1]<=u'9'))){
          std::size_t p=1;
          while(p<text.size()&&((text[p]>=u'0'&&text[p]<=u'9')||text[p]==u'.')){
            ++p;
          }
          const auto raw=ascii(text.substr(0,p));
          Value v=raw.find('.')==std::string::npos?Value{
            integer(raw)
          }
          :Value{
            decimal(raw)
          };
          const auto unit=lower(ascii(trim(text.substr(p))));
          if(!unit.empty()){
            static constexpr std::pair<std::string_view,unsigned> units[]={
              {
                "b",0
              },{
                "kb",1
              },{
                "mb",2
              },{
                "gb",3
              },{
                "tb",4
              },{
                "pb",5
              },{
                "eb",6
              },{
                "kib",11
              },{
                "mib",12
              },{
                "gib",13
              },{
                "tib",14
              },{
                "pib",15
              },{
                "eib",16
              },{
                "k",11
              },{
                "m",12
              },{
                "g",13
              },{
                "t",14
              },{
                "p",15
              },{
                "e",16
              }
            };
            std::optional<std::uint64_t> multiplier;
            for(const auto& [name,power]:units){
              if(name==unit){
                std::uint64_t n=1;
                for(unsigned i=0;i<power%10;++i){
                  n*=power>=10?1024:1000;
                }
                multiplier=n;
                break;
              }
            }
            need(multiplier.has_value(),"Invalid original numeric unit");
            if(auto n=std::get_if<std::int64_t>(&v)){
              v=wrap(static_cast<std::uint64_t>(*n) * *multiplier);
            }
            else{
              v=number(v)*static_cast<double>(*multiplier);
            }
          }
          return v;
        }
        auto value=keyword(text);
        number(value);
        return value;
      }
      Value whatever(const Ptr& arg){
        if(arg->kind!=Node::Kind::Raw){
          return eval(arg);
        }
        if(quoted(arg->raw)){
          return literal(arg->raw);
        }
        const auto s=ascii(arg->raw);
        try{
          if(s.starts_with("0x")){
            return integer(std::string_view(s).substr(2),16);
          }
          if(s.starts_with('#')){
            return integer(std::string_view(s).substr(1),16);
          }
          return decimal(s);
        }
        catch(const Rejected& error){
          if(error.problem.kind==ProblemKind::Unavailable){
            throw;
          }
        }
        const auto key=canonical(s,false);
        return key.empty()?numeric(arg):keyword(arg->raw);
      }
      Strings strings(const Ptr& arg){
        Value value;
        if(arg->kind==Node::Kind::Raw){
          value=quoted(arg->raw)?Value{
            literal(arg->raw)
          }
          :keyword(arg->raw);
        }
        else{
          value=eval(arg);
          need(std::holds_alternative<Text>(value),"Nested string expression required");
        }
        if(auto s=std::get_if<Text>(&value)){
          return {
            *s
          };
        }
        if(auto s=std::get_if<Strings>(&value)){
          return *s;
        }
        reject(ProblemKind::Invalid,"Original string/list value required");
      }
      Text string(const Ptr& arg){
        auto values=strings(arg);
        need(!values.empty(),"Empty original string list has no first value");
        return std::move(values.front());
      }
      static bool boolean(const Value& v){
        need(std::holds_alternative<bool>(v),"Boolean operand required (no numeric coercion)");
        return std::get<bool>(v);
      }
      static Text asciiLower(Text t){
        for(auto& c:t){
          if(c>127){
            reject(ProblemKind::Unavailable,"Java Locale.US Unicode case provider required");
          }
          if(c>=u'A'&&c<=u'Z'){
            c+=32;
          }
        }
        return t;
      }
      Value eval(const Ptr& node){
        if(++steps>limits.nodes*8){
          reject(ProblemKind::Bounds,"Constraint evaluation bound exceeded");
        }
        const auto& a=node->args;
        const auto& f=node->function;
        switch(node->kind){
          case Node::Kind::True:return true;
          case Node::Kind::False:return false;
          case Node::Kind::Not:return !boolean(eval(a[0]));
          case Node::Kind::Or:for(const auto& p:a){
            if(boolean(eval(p))){
              return true;
            }
          }
          return false;
          case Node::Kind::And:for(const auto& p:a){
            if(!boolean(eval(p))){
              return false;
            }
          }
          return true;
          case Node::Kind::Xor:{
            bool v=boolean(eval(a[0]));
            for(std::size_t i=1;i<a.size();++i){
              v^=boolean(eval(a[i]));
            }
            return v;
          }
          case Node::Kind::Call:break;
          default:reject(ProblemKind::Invalid,"Parameter is not an expression");
        }
        if(f=="ifthenelse"){
          return whatever(a[intValue(numeric(a[0]))!=0?1:2]);
        }
        if(oneOf(f,{
          "isge","isgt","isle","islt","iseq","isneq"
        })){
          const auto x=number(numeric(a[0])),y=number(numeric(a[1]));
          if(f=="isge"){
            return x>=y;
          }
          if(f=="isgt"){
            return x>y;
          }
          if(f=="isle"){
            return x<=y;
          }
          if(f=="islt"){
            return x<y;
          }
          if(f=="iseq"){
            return x==y;
          }
          return x!=y;
        }
        if(oneOf(f,{
          "plus","minus","mult","div","rem","min","max"
        })){
          const auto x=numeric(a[0]),y=numeric(a[1]);
          if(std::holds_alternative<std::int64_t>(x)&&std::holds_alternative<std::int64_t>(y)){
            const auto p=std::get<std::int64_t>(x),q=std::get<std::int64_t>(y);
            if(f=="plus"){
              return wrap(std::uint64_t(p)+std::uint64_t(q));
            }
            if(f=="minus"){
              return wrap(std::uint64_t(p)-std::uint64_t(q));
            }
            if(f=="mult"){
              return wrap(std::uint64_t(p)*std::uint64_t(q));
            }
            if(f=="min"){
              return std::min(p,q);
            }
            if(f=="max"){
              return std::max(p,q);
            }
            need(q!=0,"Java integer division by zero");
            if(p==INT64_MIN&&q==-1){
              return f=="div"?Value{
                p
              }
              :Value{
                std::int64_t{
                  0
                }
              };
            }
            return f=="div"?Value{
              p/q
            }
            :Value{
              p%q
            };
          }
          const auto p=number(x),q=number(y);
          if(f=="plus"){
            return p+q;
          }
          if(f=="minus"){
            return p-q;
          }
          if(f=="mult"){
            return p*q;
          }
          if(f=="div"){
            return p/q;
          }
          if(f=="rem"){
            return std::fmod(p,q);
          }
          if(std::isnan(p)||std::isnan(q)){
            return std::numeric_limits<double>::quiet_NaN();
          }
          if(f=="min"){
            return p==0&&q==0?(std::signbit(p)||std::signbit(q)?-0.0:0.0):std::min(p,q);
          }
          return p==0&&q==0?(std::signbit(p)&&std::signbit(q)?-0.0:0.0):std::max(p,q);
        }
        if(oneOf(f,{
          "hourstoseconds","daystoseconds","weekstoseconds"
        })){
          const auto d=number(numeric(a[0]));
          double converted=d;
          if(f=="weekstoseconds"){
            converted*=7;
          }
          if(f!="hourstoseconds"){
            converted*=24;
          }
          converted*=60;
          converted*=60;
          return longValue(Value{
            converted
          });
        }
        if(oneOf(f,{
          "tomb","tomib","togb","togib"
        })){
          const auto bytes=longValue(numeric(a[0]));
          const std::int64_t base=f=="tomib"||f=="togib"?1024:1000;
          return bytes/(base*base*((f=="togb"||f=="togib")?base:1));
        }
        if(f=="timetoelapsed"){
          const auto time=longValue(numeric(a[0]));
          if(time<=0){
            return time;
          }
          if(!facts.wallMilliseconds){
            reject(ProblemKind::Unavailable,"Missing owned wall clock snapshot");
          }
          const auto seconds=wrap(std::uint64_t(*facts.wallMilliseconds)-std::uint64_t(time))/1000;
          return seconds>-5&&seconds<5?std::int64_t{
            0
          }
          :seconds;
        }
        if(f=="length"){
          return static_cast<std::int64_t>(string(a[0]).size());
        }
        if(f=="count"){
          return static_cast<std::int64_t>(strings(a[0]).size());
        }
        if(f=="lowercase"){
          return asciiLower(string(a[0]));
        }
        if(f=="contains"){
          auto hay=strings(a[0]);
          auto needle=string(a[1]);
          const bool flag=a.size()==3&&intValue(Parser::numericLiteral(a[2]))==1;
          const bool alternatives=needle.find(u'|')!=Text::npos;
          const bool fold=alternatives?!flag:flag;
          std::vector<Text> needles;
          if(alternatives){
            std::size_t p=0;
            for(;;){
              const auto end=needle.find(u'|',p);
              auto n=trim(needle.substr(p,end==Text::npos?Text::npos:end-p));
              if(!n.empty()){
                needles.push_back(std::move(n));
              }
              if(end==Text::npos){
                break;
              }
              p=end+1;
            }
          }
          else{
            needles.push_back(needle);
          }
          if(alternatives&&needles.empty()){
            return !hay.empty();
          }
          if(fold){
            for(auto& n:needles){
              n=asciiLower(std::move(n));
            }
            for(auto& h:hay){
              h=asciiLower(std::move(h));
            }
          }
          for(const auto& h:hay){
            for(const auto& n:needles){
              if(!n.empty()&&h.size()>limits.workUnits/n.size()){
                reject(ProblemKind::Bounds,"Constraint substring work bound exceeded");
              }
              charge(h.size()*std::max<std::size_t>(1,n.size()));
              if(h.find(n)!=Text::npos){
                return true;
              }
            }
          }
          return false;
        }
        if(f=="hasnet"){
          const auto desired=lower(ascii(literal(a[0]->raw)));
          auto networks=fact("networks");
          need(std::holds_alternative<Strings>(networks),"Owned networks list required");
          for(const auto& n:std::get<Strings>(networks)){
            need(n==u"Public"||n==u"I2P"||n==u"Tor","Canonical owned network token required");
            if(lower(ascii(n))==desired){
              return true;
            }
          }
          return false;
        }
        if(f=="getconfig"){
          auto value=fact("config.queue.seeding.ignore.share.ratio");
          number(value);
          return value;
        }
        if(oneOf(f,{
          "isprivate","iscomplete","canarchive","isforcestart","ischecking","isstopped","ispaused","iserror","ismagnet","islownoise","isnew","issuperseeding","issequential","isshare","isunallocated","isqueued","isipfiltered","ismoving","isseeding","isdownloading","isrunning","isfriendfp"
        })){
          auto value=fact("predicate."+f);
          boolean(value);
          return value;
        }
        throw Rejected{
          {
            ProblemKind::Unavailable,f=="matches"?"Java Pattern.find/Unicode flags provider required":"Original provider/effect unavailable: "+f
          }
        };
      }
    };
    void validateFacts(const Facts& f,const Limits& l){
      if(f.values.size()>l.facts){
        reject(ProblemKind::Bounds,"Owned fact count exceeded");
      }
      std::size_t total=0;
      for(std::size_t i=0;i<f.values.size();++i){
        const auto& v=f.values[i];
        need(!v.key.empty(),"Empty fact key");
        if(v.key.size()>128){
          reject(ProblemKind::Bounds,"Owned fact key bound exceeded");
        }
        for(std::size_t j=0;j<i;++j){
          need(v.key!=f.values[j].key,"Duplicate owned fact key");
        }
        const auto text=[&](const Text& t){
          if(t.size()>l.stringUnits||t.size()>l.totalFactUnits-total){
            reject(ProblemKind::Bounds,"Owned fact text bound exceeded");
          }
          total+=t.size();
        };
        if(auto s=std::get_if<Text>(&v.value)){
          text(*s);
        }
        if(auto s=std::get_if<Strings>(&v.value)){
          if(s->size()>l.listValues){
            reject(ProblemKind::Bounds,"Owned fact list bound exceeded");
          }
          for(const auto& t:*s){
            text(t);
          }
        }
      }
    }
  }
  Result<Expression> compile(std::u16string_view source,Limits limits){
    try{
      if(source.size()>limits.sourceUnits||!limits.nodes||limits.nodes>65536||!limits.depth||limits.depth>256||!limits.arguments){
        reject(ProblemKind::Bounds,"Constraint source/resource bound exceeded");
      }
      Parser parser{limits,0,0,{}, {},false};
      Expression e;
      e.impl_=std::make_shared<const Expression::Impl>(Expression::Impl{
        parser.start(Text(source)),limits,parser.requiredTags,parser.requiresJavaPattern
      });
      return e;
    }
    catch(const Rejected& r){
      return std::unexpected(r.problem);
    }
    catch(...){
      return std::unexpected(Problem{
        ProblemKind::Bounds,"Constraint compilation resources unavailable"
      });
    }
  }
  Result<Value> evaluate(const Expression& e,const Facts& facts){
    try{
      need(bool(e.impl_),"Compiled constraint required");
      validateFacts(facts,e.impl_->limits);
      Evaluator evaluator{facts,e.impl_->limits,0,0};
      if(e.impl_->requiresJavaPattern){reject(ProblemKind::Unavailable,"Java Pattern compile provider required before rule admission");}
      if(!e.impl_->requiredTags.empty()){
        auto catalog=evaluator.fact("registered_tags");need(std::holds_alternative<Strings>(catalog),"Owned registered tag catalog required");
        for(const auto& name:e.impl_->requiredTags){need(std::find(std::get<Strings>(catalog).begin(),std::get<Strings>(catalog).end(),name)!=std::get<Strings>(catalog).end(),"Original constraint tag name not found");}
      }
      return evaluator.eval(e.impl_->root);
    }
    catch(const Rejected& r){
      return std::unexpected(r.problem);
    }
    catch(...){
      return std::unexpected(Problem{
        ProblemKind::Bounds,"Constraint evaluation resources unavailable"
      });
    }
  }
  Result<bool> truth(const Value& value){
    try{
      if(auto b=std::get_if<bool>(&value)){
        return *b;
      }
      return intValue(value)!=0;
    }
    catch(const Rejected& r){
      return std::unexpected(r.problem);
    }
  }
  Options parseOptions(std::string_view raw)noexcept{
    if(raw.find("am=3;")!=raw.npos){
      return {
        false,false,true
      };
    }
    return {
      raw.find("am=2;")==raw.npos,raw.find("am=1;")==raw.npos,false
    };
  }
  Result<Property> readProperty(const std::vector<std::optional<Text>>& raw,bool enabled,Limits limits){
    try{
      if(raw.size()>limits.arguments){
        reject(ProblemKind::Bounds,"Constraint property list bound exceeded");
      }
      Property p;
      p.enabled=enabled;
      if(!raw.empty()&&raw[0]){
        p.expression=trim(*raw[0]);
      }
      if(raw.size()>1&&raw[1]){
        p.rawOptions=trim(*raw[1]);
      }
      if(p.expression.size()>limits.sourceUnits||p.rawOptions.size()>limits.sourceUnits){
        reject(ProblemKind::Bounds,"Constraint property text bound exceeded");
      }
      p.configured=!p.expression.empty();
      if(p.rawOptions.find(u"am=3;")!=Text::npos){
        p.mode={
          false,false,true
        };
      }
      else{
        p.mode={
          p.rawOptions.find(u"am=2;")==Text::npos,p.rawOptions.find(u"am=1;")==Text::npos,false
        };
      }
      return p;
    }
    catch(const Rejected& r){
      return std::unexpected(r.problem);
    }
    catch(...){
      return std::unexpected(Problem{
        ProblemKind::Bounds,"Constraint property resources unavailable"
      });
    }
  }
  Decision project(const Result<Value>& evaluated,Options options,bool enabled,bool isNew,bool member){
    if(!enabled||(options.newOnly&&!isNew)){
      return {
      };
    }
    if(!evaluated){
      return {
        Membership::Keep,evaluated.error()
      };
    }
    auto yes=truth(*evaluated);
    if(!yes){
      return {
        Membership::Keep,yes.error()
      };
    }
    if(*yes&&!member&&(options.add||(options.newOnly&&isNew))){
      return {
        Membership::Add,{
        }
      };
    }
    if(!*yes&&member&&options.remove&&!options.newOnly){
      return {
        Membership::Remove,{
        }
      };
    }
    return {
    };
  }
  Decision project(const Result<Value>& value,const Property& property,bool isNew,bool member){
    return project(value,property.mode,property.enabled&&property.configured,isNew,member);
  }
}
