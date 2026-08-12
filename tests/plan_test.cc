#include "btrc/plan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "tests/test_trees.h"

namespace {

void Check(bool condition) {
  if (!condition)
    throw std::runtime_error("plan test failed");
}

template <class Function> void CheckInvalid(Function &&function) {
  bool threw = false;
  try {
    function();
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  Check(threw);
}

std::size_t ProvenDepthBound(std::size_t nodes) {
  if (nodes == 0)
    throw std::invalid_argument("the depth bound requires a nonempty tree");
  const double structural_rounds =
      std::ceil(std::log(static_cast<double>(nodes)) / std::log(1.5));
  return static_cast<std::size_t>(std::floor(
      std::log2(2.0 * static_cast<double>(nodes) - 1.0) +
      structural_rounds * std::log2(36.0)));
}

void CheckDependencyLevels(const btrc::Plan &plan) {
  Check(plan.uses_dependency_levels());
  std::vector<int> node_producer(plan.num_nodes(), -1);
  std::vector<int> edge_producer(plan.num_edges(), -1);
  std::vector<int> branch_producer(plan.num_branches(), -1);

  for (std::size_t level_index = 0;
       level_index < plan.dependency_levels().size(); ++level_index) {
    const btrc::DependencyLevel &level =
        plan.dependency_levels()[level_index];
    const int producer_level = static_cast<int>(level_index);
    Check(!level.rakes.empty() || !level.branch_combinations.empty() ||
          !level.branch_absorptions.empty() || !level.compressions.empty());
    std::vector<int> node_writer(plan.num_nodes(), -1);
    std::vector<int> edge_writer(plan.num_edges(), -1);
    std::vector<int> branch_writer(plan.num_branches(), -1);
    const auto register_write = [](std::vector<int> &writers,
                                   btrc::Index index, int operation) {
      Check(writers[index] == -1);
      writers[index] = operation;
    };
    const auto check_read = [](const std::vector<int> &writers,
                               btrc::Index index, int operation) {
      Check(writers[index] == -1 || writers[index] == operation);
    };

    int operation = 0;
    for (const btrc::Rake &rake : level.rakes)
      register_write(branch_writer, rake.branch, operation++);
    for (const btrc::BranchCombination &combination :
         level.branch_combinations) {
      register_write(branch_writer, combination.destination, operation++);
    }
    for (const btrc::BranchAbsorption &absorption :
         level.branch_absorptions) {
      register_write(node_writer, absorption.parent, operation++);
    }
    for (const btrc::Compression &compression : level.compressions)
      register_write(edge_writer, compression.left_edge, operation++);

    operation = 0;
    for (const btrc::Rake &rake : level.rakes) {
      Check(edge_producer[rake.edge] < producer_level);
      Check(node_producer[rake.leaf] < producer_level);
      check_read(edge_writer, rake.edge, operation);
      check_read(node_writer, rake.leaf, operation);
      ++operation;
    }
    for (const btrc::BranchCombination &combination :
         level.branch_combinations) {
      Check(branch_producer[combination.destination] < producer_level);
      Check(branch_producer[combination.source] < producer_level);
      check_read(branch_writer, combination.destination, operation);
      check_read(branch_writer, combination.source, operation);
      ++operation;
    }
    for (const btrc::BranchAbsorption &absorption :
         level.branch_absorptions) {
      Check(node_producer[absorption.parent] < producer_level);
      Check(branch_producer[absorption.branch] < producer_level);
      check_read(node_writer, absorption.parent, operation);
      check_read(branch_writer, absorption.branch, operation);
      ++operation;
    }
    for (const btrc::Compression &compression : level.compressions) {
      Check(node_producer[compression.middle] < producer_level);
      Check(edge_producer[compression.left_edge] < producer_level);
      Check(edge_producer[compression.right_edge] < producer_level);
      check_read(node_writer, compression.middle, operation);
      check_read(edge_writer, compression.left_edge, operation);
      check_read(edge_writer, compression.right_edge, operation);
      ++operation;
    }

    for (std::size_t node = 0; node < node_writer.size(); ++node) {
      if (node_writer[node] != -1)
        node_producer[node] = producer_level;
    }
    for (std::size_t edge = 0; edge < edge_writer.size(); ++edge) {
      if (edge_writer[edge] != -1)
        edge_producer[edge] = producer_level;
    }
    for (std::size_t branch = 0; branch < branch_writer.size(); ++branch) {
      if (branch_writer[branch] != -1)
        branch_producer[branch] = producer_level;
    }
  }
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
    CheckInvalid([] { btrc::MakePlan(std::vector<std::int64_t>{}); });
    CheckInvalid(
        [] { btrc::MakePlan(std::vector<std::int64_t>{-1, -1}); });
    CheckInvalid([] { btrc::MakePlan(std::vector<std::int64_t>{-1, 2}); });
    CheckInvalid([] { btrc::MakePlan(std::vector<std::int64_t>{-1, 1}); });
    CheckInvalid(
        [] { btrc::MakePlan(std::vector<std::int64_t>{-1, 2, 1}); });
    CheckInvalid([] {
      btrc::MakePlan(std::vector<std::int64_t>{1, -1}, btrc::Index{0});
    });
  }

  {
    const btrc::Plan singleton =
        btrc::MakePlan(std::vector<std::int64_t>{0});
    Check(singleton.root() == 0);
    Check(singleton.num_edges() == 0);
    Check(singleton.primitive_batches().empty());

    const btrc::Plan explicit_root = btrc::MakePlan(
        std::vector<std::int64_t>{2, 0, 2, 2}, btrc::Index{2});
    Check(explicit_root.root() == 2);
    Check((std::vector<std::int64_t>(explicit_root.parents().begin(),
                                     explicit_root.parents().end()) ==
           std::vector<std::int64_t>{2, 0, -1, 2}));
  }

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
    const std::vector<std::int64_t> parents =
        btrc_test::DelayedStar(6, 16);
    const btrc::Plan plan = btrc::MakePlan(parents);
    const btrc::PlanStatistics stats = btrc::Statistics(plan);
    Check(plan.uses_dependency_levels());
    Check(!plan.primitive_batches().empty());
    Check(plan.rakes().size() == stats.rakes);
    Check(plan.compressions().size() == stats.compressions);
    Check(stats.primitive_levels == 16);
    Check(stats.primitive_levels <=
          2 * static_cast<std::size_t>(std::ceil(std::log2(parents.size()))));
    Check(stats.primitive_levels <= ProvenDepthBound(parents.size()));
    CheckDependencyLevels(plan);
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
      Check(stats.primitive_levels <= ProvenDepthBound(parents.size()));
    }
  }
}
