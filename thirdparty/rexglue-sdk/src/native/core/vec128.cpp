#include <cstddef>
#include <ostream>
#include <string>

#include <fmt/format.h>

#include <native/vec128.h>

namespace rex {

std::string to_string(const vec128_t& value) {
  return fmt::format("({}, {}, {}, {})", value.x, value.y, value.z, value.w);
}

std::ostream& operator<<(std::ostream& os, const vec128_t& value) {
  os << fmt::format("[{:08X} {:08X} {:08X} {:08X}]",
                    value.u32[0], value.u32[1], value.u32[2], value.u32[3]);
  return os;
}

}  // namespace rex
