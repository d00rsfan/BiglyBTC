#pragma once
#include <cstddef>
#include <memory>

namespace biglybtc::test::app_close_fault {
// Isolated executable link wrappers only. No production fault API or DSO edits.
class Scope final {
public:
  Scope();
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  void rejectNextSubmit();
  void failFirstAllocationAfterJoinedSession();
  std::size_t rejectedSubmissions() const;
  std::size_t joinedSessions() const;
  std::size_t allocationFailures() const;
private:
  struct Control;
  std::unique_ptr<Control> control_;
};
} // namespace biglybtc::test::app_close_fault
