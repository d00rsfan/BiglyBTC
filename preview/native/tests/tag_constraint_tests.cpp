// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/tag_constraints.hpp"
#include "../app/constraint_cadence.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace c=biglybtc::core::tags::constraints;
namespace {
  void check(bool v,const char* why){
    if(!v){
      throw std::runtime_error(why);
    }
  }
  c::Result<c::Value> run(std::u16string_view text,const c::Facts& facts={
  }){
    auto expression=c::compile(text);
    check(bool(expression),"Unexpected source compilation refusal");
    return c::evaluate(*expression,facts);
  }
  void yes(std::u16string_view text,bool expected,const c::Facts& facts={
  }){
    auto v=run(text,facts);
    check(bool(v),"Unexpected unavailable/error value");
    auto b=c::truth(*v);
    check(b&&*b==expected,"Original truth result differs");
  }
  void unavailable(std::u16string_view text,const c::Facts& facts={
  }){
    auto v=run(text,facts);
    check(!v&&v.error().kind==c::ProblemKind::Unavailable,"Missing provider became a false/available value");
    check(c::project(v,{
    },true,false,true).membership==c::Membership::Keep,"Unavailable rule removed existing membership");
  }
  void grammarAndExactOrder(){
    yes(u"",false);
    yes(u"TrUe",true);
    yes(u"(/*comment*/true)",true);
    check(!c::compile(u"true||false"),"Source basic grammar unexpectedly accepted bare chained literal");
    check(!c::compile(u"!true"),"Source basic grammar unexpectedly accepted ungrouped true");
    yes(u"(true)||(false)",true);
    yes(u"!(false)",true);
    yes(u"(true)^(true)^(true)",true);
    yes(u"(8*2+1)==24",true);
    yes(u"(10-3-2)==9",true);
    yes(u"((10-3)-2)==5",true);
    yes(u"isEQ(plus(1,mult(2,3)),7)",true);
    yes(u"isEQ(isEQ(1,1)?42:javascript(\"unreached\"),42)",true);
    yes(u"(false)&&javascript(\"unreached\")",false);
    unavailable(u"(true)||matches(name,\"x\")");
    unavailable(u"(false)&&hasTag(\"not-bound\")");
    unavailable(u"javascript(\"missing\")||(true)");
    unavailable(u"(true)^javascript(\"missing\")");
    check(!c::compile(u"unknownFunction()||(true)"),"Unknown source function must fail compilation even if unreachable");
    check(!c::compile(u"contains(\"https://example\",\"example\")"),"Original prequote line-comment behavior changed");
    check(!c::compile(u"isEQ(1,2"),"Unmatched brackets accepted");
    auto bound=c::compile(u"isEQ(1,1)",c::Limits{
      1
    });
    check(!bound&&bound.error().kind==c::ProblemKind::Bounds,"Source bound ignored");
  }
  void javaNumbersAndConversions(){
    yes(u"isEQ(plus(max64,1),min64)",true);
    yes(u"isEQ(div(min64,-1),min64)",true);
    yes(u"isEQ(rem(min64,-1),0)",true);
    yes(u"isEQ(9007199254740992,9007199254740993)",true);
    yes(u"isEQ(1KiB,1024)",true);
    yes(u"isEQ(2KB,2000)",true);
    yes(u"isEQ(2m,2097152)",true);
    yes(u"isEQ(tomb(2097152),2)",true);
    yes(u"isEQ(tomib(2097152),2)",true);
    yes(u"isEQ(hourstoseconds(1.5),5400)",true);
    yes(u"isEQ(w2s(2),1209600)",true);
    yes(u"isEQ(ifThenElse(isEQ(1,1),\"42\",javascript(\"x\")),42)",true);
    auto division=run(u"div(1,0)");
    check(!division&&division.error().kind==c::ProblemKind::Invalid,"Integer divide by zero silently became false");
    auto nan=run(u"div(0.0,0.0)");
    check(nan&&std::isnan(std::get<double>(*nan)),"IEEE Java double NaN lost");
    auto negative=run(u"min(-0.0,0.0)");
    check(negative&&std::signbit(std::get<double>(*negative)),"Java Math.min negative zero lost");
    const auto narrowed=c::truth(c::Value{
      std::int64_t{
        4294967296
      }
    });
    check(narrowed&&!*narrowed,"Number.intValue must narrow to32 bits");
    auto wrong=run(u"!(1+2)");
    check(!wrong&&wrong.error().kind==c::ProblemKind::Invalid,"Boolean operand silently coerced a number");
    c::Facts clock;
    clock.wallMilliseconds=10000;
    yes(u"isEQ(timeToElapsed(9000),0)",true,clock);
    yes(u"isEQ(timeToElapsed(1000),9)",true,clock);
    unavailable(u"timeToElapsed(1000)");
  }
  void stringsFactsAndProviderSeparation(){
    c::Facts facts;
    facts.values={
      {
        "name",c::Text(u"Ubuntu ISO")
      },{
        "filenames",c::Strings{
          u"one",u"second"
        }
      },{
        "size",std::int64_t{
          16384
        }
      },{
        "predicate.iscomplete",false
      },{
        "networks",c::Strings{
          u"Public",u"I2P"
        }
      }
    };
    yes(u"size==16KiB",true,facts);
    yes(u"isComplete()",false,facts);
    yes(u"hasNet(\"i2p\")",true,facts);
    yes(u"isEQ(length(\"\U0001f600\"),2)",true);
    yes(u"isEQ(count(file_names),2)",true,facts);
    yes(u"isEQ(length(file_names),3)",true,facts);
    yes(u"contains(name,\"ubuntu\")",false,facts);
    yes(u"contains(name,\"ubuntu\",1)",true,facts);
    yes(u"contains(\"HELLO\",\"none|hello\")",true);
    yes(u"contains(\"HELLO\",\"none|hello\",1)",false);
    yes(u"contains(lowercase(name),\"ubuntu\")",true,facts);
    yes(u"contains(\"\u00c4\",\"\u00e4\")",false);
    unavailable(u"contains(\"\u00c4\",\"\u00e4\",1)");
    unavailable(u"lowercase(\"\u00c4\")");
    unavailable(u"matches(name,\"^u.*\")",facts);
    unavailable(u"isComplete()");
    unavailable(u"size>0");
    unavailable(u"hasTag(\"not-bound\")");
    unavailable(u"setColors(255)");
    auto duplicate=facts;
    duplicate.values.push_back(facts.values.front());
    auto v=run(u"isEQ(1,1)",duplicate);
    check(!v&&v.error().kind==c::ProblemKind::Invalid,"Ambiguous fact key accepted");
  }
  void exactOptionsAndSafeMembership(){
    const auto both=c::parseOptions("");
    check(both.add&&both.remove&&!both.newOnly,"Default options changed");
    check(c::parseOptions("am=1;").add&&!c::parseOptions("am=1;").remove,"Add-only options changed");
    check(!c::parseOptions("am=2;").add&&c::parseOptions("am=2;").remove,"Remove-only options changed");
    check(c::parseOptions("xam=3;am=1;").newOnly,"Original substring/new-only priority changed");
    check(!c::parseOptions("am=3").newOnly,"Missing-semicolon source behavior normalized away");
    const c::Result<c::Value> t=c::Value{
      true
    },f=c::Value{
      false
    };
    check(c::project(t,both,true,false,false).membership==c::Membership::Add,"True add decision missing");
    check(c::project(f,both,true,false,true).membership==c::Membership::Remove,"Available false removal missing");
    check(c::project(f,c::parseOptions("am=1;"),true,false,true).membership==c::Membership::Keep,"Add-only removed membership");
    check(c::project(t,c::parseOptions("am=3;"),true,false,false).membership==c::Membership::Keep,"New-only affected old download");
    check(c::project(t,c::parseOptions("am=3;"),true,true,false).membership==c::Membership::Add,"New-only failed new add");
    check(c::project(f,c::parseOptions("am=3;"),true,true,true).membership==c::Membership::Keep,"New-only removed membership");
    check(c::project(f,both,false,false,true).membership==c::Membership::Keep,"Disabled constraint changed membership");
    auto empty=c::readProperty({
    });
    check(empty&&!empty->configured&&c::project(f,*empty,false,true).membership==c::Membership::Keep,"Absent property became an active false/removing rule");
    auto property=c::readProperty({
      c::Text(u"  isComplete()  "),c::Text(u" am=1; "),c::Text(u"opaque-future")
    });
    check(property&&property->expression==u"isComplete()"&&property->rawOptions==u"am=1;"&&!property->mode.remove,"Original string-list property projection changed");
    auto disabled=c::readProperty({
      c::Text(u"false")
    },false);
    check(disabled&&c::project(f,*disabled,false,true).membership==c::Membership::Keep,"Saved disabled property removed membership");
  }
  void originalTimeClassificationAndCadence(){
    namespace detail=biglybtc::app::detail;using Cadence=detail::ConstraintCadence;using Clock=Cadence::Clock;
    const auto timed=[](std::u16string_view text,bool expected){auto expression=c::compile(text);check(expression&&expression->dependsOnTime()==expected,"Original parsed time dependency differs");};
    for(const auto* token:{u"age",u"seedcount",u"seed_count",u"peercount",u"peer_count",u"seedpeerratio",u"seed_peer_ratio",u"resumein",u"resume_in",u"minofhour",u"min_of_hour",u"hourofday",u"hour_of_day",u"dayofweek",u"day_of_week",u"tagage",u"tag_age",u"completedage",u"completed_age",u"targetratio",u"target_ratio",u"my_rating",u"myrating"}){
      timed(std::u16string(token)+u">=0",true);
    }
    timed(u"(true)||(tag_age>=0)",true);timed(u"ifThenElse(isEQ(1,1),age,0)",true);
    timed(u"contains(\"tag_age\",\"age\")",false);timed(u"hasTag(\"age\")",false);timed(u"timeToElapsed(0)>=0",false);
    timed(u"size>0",false);timed(u"session_age>0",false);timed(u"(false)&&isEQ(downloading_for,0)",false);
    check(!c::Expression{}.dependsOnTime(),"Uncompiled value acquired time scheduling");
    const auto zero=Clock::time_point{};Cadence cadence;
    auto due=cadence.poll(zero,30);check(!due.full&&!due.time,"New timer fired immediately");
    due=cadence.poll(zero+std::chrono::milliseconds(4999),30);check(!due.full&&!due.time,"Early5s tick");
    due=cadence.poll(zero+std::chrono::seconds(5),30);check(due.time&&!due.full,"Exact5s time tick missing");
    due=cadence.poll(zero-std::chrono::hours(1),30);check(!due.full&&!due.time,"Backward observation advanced timer");
    due=cadence.poll(zero+std::chrono::seconds(9),30);check(!due.full&&!due.time,"Wall-independent tick moved after backward observation");
    for(int tick=2;tick<=6;++tick){due=cadence.poll(zero+std::chrono::seconds(5*tick),30);check(due.full==(tick==6)&&due.time==(tick!=6),"Full apply did not suppress same-tick time duplicate");if(due.full){cadence.fullQueued();}}
    Cadence disabled;disabled.poll(zero,0);due=disabled.poll(zero+std::chrono::seconds(5),0);check(due.full&&!due.time,"Disabled full period lost original first timer apply");
    disabled.fullQueued();due=disabled.poll(zero+std::chrono::seconds(10),0);check(due.time&&!due.full,"Disabled full period disabled time rules");
    due=disabled.poll(zero+std::chrono::seconds(1000),0);check(due.time&&!due.full,"Delayed callback lost time work");
    due=disabled.poll(zero+std::chrono::seconds(1000),0);check(!due.full&&!due.time,"Delayed timer burst historical callbacks");
    Cadence rounded;rounded.poll(zero,11);due=rounded.poll(zero+std::chrono::seconds(5),11);check(due.time,"First11s tick changed");due=rounded.poll(zero+std::chrono::seconds(10),11);check(due.full&&!due.time,"Original integer period/5 was rounded up");
    Cadence limits;limits.poll(Clock::time_point::max(),0);check(!limits.ready(Clock::time_point::max()),"Clock range overflow rearmed an immediate loop");
    auto wrapped=Cadence::dueForTick(0x80000000U,10,true);check(wrapped.full&&!wrapped.time,"Java signed tick wrap changed modulo boundary");
    disabled.reset();due=disabled.poll(zero,0);check(!due.full&&!due.time,"Terminal reset fired immediately");
  }
}
int main(){
  try{
    grammarAndExactOrder();
    javaNumbersAndConversions();
    stringsFactsAndProviderSeparation();
    exactOptionsAndSafeMembership();
    originalTimeClassificationAndCadence();
    std::cout<<"PASS source-compatible constraint value families\n";
    return 0;
  }
  catch(const std::exception& e){
    std::cerr<<"FAIL "<<e.what()<<'\n';
    return 1;
  }
}
