#pragma once

#include <cstddef>

namespace rocky::smr::ebr {

namespace detail {
class Cohort;
struct Participant;
} // namespace detail

class Guard {
public:
  Guard() noexcept;
  Guard(Guard&& other) noexcept;
  Guard& operator=(Guard&& other) noexcept;
  ~Guard();

  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;

  [[nodiscard]] bool Empty() const noexcept;
  void ResetProtection(std::nullptr_t = nullptr) noexcept;
  void Swap(Guard& other) noexcept;

private:
  friend Guard MakeGuard();

  Guard(detail::Cohort* cohort, detail::Participant* participant) noexcept;

  detail::Cohort* cohort;
  detail::Participant* participant;
};

Guard MakeGuard();
void Swap(Guard& a, Guard& b) noexcept;

} // namespace rocky::smr::ebr
