#ifndef BTRC_TESTS_TEST_TREES_H_
#define BTRC_TESTS_TEST_TREES_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace btrc_test {

inline std::vector<std::int64_t> DelayedStar(std::size_t groups,
                                             std::size_t width) {
  std::vector<std::int64_t> parents{-1};
  for (std::size_t group = 0; group < groups; ++group) {
    for (std::size_t branch = 0; branch < width; ++branch) {
      std::int64_t parent = 0;
      for (std::size_t depth = 0; depth < (std::size_t{1} << group); ++depth) {
        parents.push_back(parent);
        parent = static_cast<std::int64_t>(parents.size() - 1);
      }
    }
  }
  return parents;
}

} // namespace btrc_test

#endif // BTRC_TESTS_TEST_TREES_H_
