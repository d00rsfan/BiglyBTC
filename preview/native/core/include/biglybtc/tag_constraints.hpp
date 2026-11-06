// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
namespace biglybtc::core::tags::constraints {
  // UTF16 preserves Java String length/search and quoted source spelling. No
  // locale, filesystem, tag manager, clock, regex engine or callback is borrowed.
  using Text=std::u16string;
  using Strings=std::vector<Text>;
  using Value=std::variant<bool,std::int64_t,float,double,Text,Strings>;
  enum class ProblemKind {
    Invalid,Unavailable,Bounds
  };
  struct Problem {
    ProblemKind kind;
    std::string detail;
  };
  template<class T>using Result=std::expected<T,Problem>;
  struct Limits {
    std::size_t sourceUnits=16384,nodes=2048,depth=64,arguments=64;
    std::size_t facts=128,stringUnits=65536,listValues=4096,totalFactUnits=262144,workUnits=8388608;
  };
  struct Fact {
    std::string key;
    Value value;
  };
  struct Facts {
    std::vector<Fact> values;
    // Owning source-defined canonical keys; no providers.
    std::optional<std::int64_t> wallMilliseconds;
  };
  class Expression final {
    public:
    struct Impl;
    Expression()=default;
    // Original parsed-keyword DEP_TIME classification, not a provider/clock grant.
    bool dependsOnTime()const noexcept;
    private:
    std::shared_ptr<const Impl> impl_;
    friend Result<Expression> compile(std::u16string_view,Limits);
    friend Result<Value> evaluate(const Expression&,const Facts&);
  };
  Result<Expression> compile(std::u16string_view,Limits={
  });
  Result<Value> evaluate(const Expression&,const Facts&);
  // Original outer testConstraint Number.intValue !=0 conversion, with strict
  // Boolean operands within !/&&/||/^ (their original casts are not coercions).
  Result<bool> truth(const Value&);
  struct Options {
    bool add=true,remove=true,newOnly=false;
  };
  Options parseOptions(std::string_view raw)noexcept;
  // Exact am=N; substring rules.
  struct Property {
    Text expression,rawOptions;
    Options mode;
    bool enabled=true,configured=false;
  };
  // Original pp.constraint String[]: trimmed first expression/second options;
  // null/missing entries are empty. Empty property is inactive, not a false rule.
  // pp.enabled.constraint defaults true. No original bytes are rewritten here.
  Result<Property> readProperty(const std::vector<std::optional<Text>>&,bool enabled=true,Limits={
  });
  enum class Membership {
    Keep,Add,Remove
  };
  struct Decision {
    Membership membership=Membership::Keep;
    std::optional<Problem> problem;
  };
  // An unavailable/error result NEVER authorizes removal or addition. No saved
  // membership or actual tag/service/payload action is changed by this function.
  Decision project(const Result<Value>&,Options,bool enabled,bool isNew,bool member);
  Decision project(const Result<Value>&,const Property&,bool isNew,bool member);
}
