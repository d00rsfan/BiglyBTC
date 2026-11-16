// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/tag_constraints.hpp>
#include <cstdint>
#include <optional>
#include <span>
namespace biglybtc::core::tags::dependency {
// Pure control protocol. These requests/tokens grant no source, membership,
// profile or effect authority. An owner must separately evaluate, fence and CAS.
enum class Error {Invalid,Bounds,Unavailable,Memory};
struct Node {
  constraints::Options mode;
  bool enabled=true,member=false,mayAdd=true;
  std::vector<std::size_t> dependencies;
};
struct Limits {std::size_t nodes=4096,edges=8192,depth=256,steps=32768;};
enum class Action {Evaluate,Add,Remove,Finished};
struct Request {
  Action action=Action::Finished;std::size_t node=0;std::uint64_t sequence=0;
  bool operator==(const Request&)const=default;
};
// Explicit stack ports applySupport2, including the shared ORIGINAL root
// membership snapshot, path-local checked set and dependency recheck with checks
// disabled. No callbacks, IO, clocks or recursive native call stack are used.
class Apply final {
public:
 static std::expected<Apply,Error> create(std::vector<Node>,std::size_t root,
     bool checkDependencies,bool isNew,std::vector<std::size_t> checked={},Limits={});
 std::expected<Request,Error> next();
 // Evaluate accepts truth; Add/Remove accept whether the separately authorised
 // effect actually took place. Exact sequence tokens reject stale recheck replies.
 std::expected<void,Error> accept(const Request&,bool result);
 void unavailable()noexcept{failure_=Error::Unavailable;pending_.reset();}
 void stopping()noexcept{stopping_=true;}
 bool member(std::size_t node)const noexcept{return node<nodes_.size()&&nodes_[node].member;}
 bool checked(std::size_t node)const noexcept{return node<checked_.size()&&checked_[node];}
private:
 enum class Phase {Enter,Evaluation,Branch,Dependencies,Recheck,Effect,Done};
 struct Frame {std::size_t node=0,cursor=0;bool check=true,truth=false;Phase phase=Phase::Enter;};
 std::vector<Node> nodes_;std::vector<Frame> stack_;std::vector<bool> checked_;
 Limits limits_;std::optional<Request> pending_;std::optional<Error> failure_;
 bool existing_=false,isNew_=false,stopping_=false;
 std::size_t steps_=0;std::uint64_t sequence_=0;
};
}
