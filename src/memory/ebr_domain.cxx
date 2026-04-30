#include "ebr_domain.hxx"
#include "utils.hxx"

#include <atomic>
#include <cassert>

namespace rocky {

namespace smr::ebr {

thread_local ThreadState tl_State;

} // namespace smr::ebr

} // namespace rocky