#include "btrc/plan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

void Check(bool condition) {
  if (!condition)
    throw std::runtime_error("plan test failed");
}

std::vector<std::int64_t> DelayedStar(std::size_t groups,
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

void AppendChain(std::vector<std::int64_t> &parents, std::int64_t parent,
                 std::size_t length) {
  for (std::size_t index = 0; index < length; ++index) {
    parents.push_back(parent);
    parent = static_cast<std::int64_t>(parents.size() - 1);
  }
}

void AppendDelayedGadget(std::vector<std::int64_t> &parents,
                         std::int64_t parent, std::size_t level,
                         std::size_t width) {
  parents.push_back(parent);
  const std::int64_t root = static_cast<std::int64_t>(parents.size() - 1);
  if (level == 0) {
    parents.push_back(root);
    return;
  }

  std::int64_t continuation = root;
  for (std::size_t index = 0; index < (std::size_t{1} << (level - 1));
       ++index) {
    parents.push_back(continuation);
    continuation = static_cast<std::int64_t>(parents.size() - 1);
  }
  AppendDelayedGadget(parents, continuation, level - 1, width);
  for (std::size_t branch = 0; branch < width; ++branch)
    AppendChain(parents, root, std::size_t{1} << level);
}

std::vector<std::int64_t> RecursivelyDelayedRakes(std::size_t depth,
                                                  std::size_t width) {
  std::vector<std::int64_t> parents{-1};
  AppendDelayedGadget(parents, 0, depth, width);
  return parents;
}

} // namespace

int main() {
  {
    const std::vector<std::int64_t> parents{4, 4, 0, 1, -1, 1, 5, 2, 2};
    const btrc::Plan plan = btrc::MakePlan(parents);
    std::vector<btrc::Index> removed;
    for (const btrc::Round &round : plan.rounds()) {
      for (const btrc::Rake &rake : round.rakes)
        removed.push_back(rake.leaf);
      for (const btrc::Compression &compression : round.compressions)
        removed.push_back(compression.middle);
    }
    std::sort(removed.begin(), removed.end());
    Check((removed == std::vector<btrc::Index>{0, 1, 2, 3, 5, 6, 7, 8}));
  }

  {
    const btrc::Plan plan = btrc::MakePlan(
        std::vector<std::int64_t>{-1, 0, 1, 2, 3, 4});
    const std::span<const btrc::Round> rounds = plan.rounds();
    const auto &compressions = rounds.front().compressions;
    Check(compressions.size() == 2);
    Check(compressions[0].middle == 1);
    Check(compressions[0].parent == 0);
    Check(compressions[0].child == 2);
    Check(compressions[1].middle == 3);
    Check(compressions[1].parent == 2);
    Check(compressions[1].child == 4);
  }

  {
    constexpr std::size_t kNodes = 1025;
    std::vector<std::int64_t> parents(kNodes, 0);
    parents[0] = -1;
    const btrc::Plan plan = btrc::MakePlan(parents);
    const btrc::PlanStatistics stats = btrc::Statistics(plan);
    Check(stats.rounds == 1);
    Check(stats.rakes == kNodes - 1);
    Check(plan.rounds().front().branch_reduction_stages.size() ==
           static_cast<std::size_t>(std::ceil(std::log2(kNodes - 1))));
  }


  {
    const std::vector<std::int64_t> parents = DelayedStar(6, 16);
    const btrc::Plan plan = btrc::MakePlan(parents);
    const btrc::PlanStatistics stats = btrc::Statistics(plan);
    Check(plan.uses_dependency_levels());
    Check(stats.primitive_levels == 16);
    Check(stats.primitive_levels <=
          2 * static_cast<std::size_t>(std::ceil(std::log2(parents.size()))));
  }

  {
    for (std::size_t depth = 2; depth < 8; ++depth) {
      const std::vector<std::int64_t> parents =
          RecursivelyDelayedRakes(depth, std::size_t{1} << depth);
      const btrc::Plan plan = btrc::MakePlan(parents);
      const btrc::PlanStatistics stats = btrc::Statistics(plan);
      Check(stats.primitive_levels <=
            4 * static_cast<std::size_t>(
                    std::ceil(std::log2(parents.size()))));
    }
  }
}
