#include "btrc/execute.h"

#include <cstdint>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

#include "tests/test_trees.h"

namespace {

void Check(bool condition) {
  if (!condition)
    throw std::runtime_error("execution test failed");
}

} // namespace

class SubtreeSumDispatcher {
public:
  SubtreeSumDispatcher(const btrc::Plan &plan, std::vector<int> values)
      : nodes_(std::move(values)), paths_(plan.num_edges(), 0),
        branches_(plan.num_branches(), 0), residuals_(plan.num_nodes(), 0),
        outputs_(plan.num_nodes(), 0) {}

  void Rake(std::span<const btrc::Rake> operations) {
    for (const auto &op : operations) {
      branches_[op.branch] = paths_[op.edge] + nodes_[op.leaf];
      residuals_[op.leaf] = nodes_[op.leaf];
    }
  }
  void CombineBranches(std::span<const btrc::BranchCombination> operations) {
    for (const auto &op : operations)
      branches_[op.destination] += branches_[op.source];
  }
  void AbsorbBranches(std::span<const btrc::BranchAbsorption> operations) {
    for (const auto &op : operations)
      nodes_[op.parent] += branches_[op.branch];
  }
  void Compress(std::span<const btrc::Compression> operations) {
    for (const auto &op : operations) {
      residuals_[op.middle] = nodes_[op.middle] + paths_[op.right_edge];
      paths_[op.left_edge] += residuals_[op.middle];
    }
  }
  void ExpandCompressions(std::span<const btrc::Compression> operations) {
    for (const auto &op : operations)
      outputs_[op.middle] = residuals_[op.middle] + outputs_[op.child];
  }
  void ExpandRakes(std::span<const btrc::Rake> operations) {
    for (const auto &op : operations)
      outputs_[op.leaf] = residuals_[op.leaf];
  }

  int Root(btrc::Index root) const { return nodes_[root]; }
  void SeedRoot(btrc::Index root) { outputs_[root] = nodes_[root]; }
  const std::vector<int> &outputs() const { return outputs_; }

private:
  std::vector<int> nodes_;
  std::vector<int> paths_;
  std::vector<int> branches_;
  std::vector<int> residuals_;
  std::vector<int> outputs_;
};

int main() {
  {
    const btrc::Plan plan =
        btrc::MakePlan(std::vector<std::int64_t>{-1, 0, 0, 1, 1, 3, 2});
    SubtreeSumDispatcher dispatcher(plan, {1, 2, 3, 4, 5, 6, 7});
    btrc::Contract(plan, dispatcher);
    Check(dispatcher.Root(plan.root()) == 28);
    dispatcher.SeedRoot(plan.root());
    btrc::Expand(plan, dispatcher);
    Check((dispatcher.outputs() ==
           std::vector<int>{28, 17, 10, 10, 5, 6, 7}));
  }

  // The adversarial delayed-star family selects earliest-start dependency
  // levels rather than structural rounds. Exercise that representation through
  // the same public traversal API.
  {
    const std::vector<std::int64_t> parents =
        btrc_test::DelayedStar(6, 16);
    const btrc::Plan plan = btrc::MakePlan(parents);
    Check(plan.uses_dependency_levels());
    std::vector<int> values(parents.size(), 1);
    std::vector<int> expected(values);
    for (std::size_t node = parents.size(); node-- > 1;)
      expected[static_cast<std::size_t>(parents[node])] += expected[node];
    SubtreeSumDispatcher dispatcher(plan, values);
    btrc::Contract(plan, dispatcher);
    Check(dispatcher.Root(plan.root()) == expected[plan.root()]);
    dispatcher.SeedRoot(plan.root());
    btrc::Expand(plan, dispatcher);
    Check(dispatcher.outputs() == expected);
  }

  // Exercise arbitrary node numbering rather than only topological parent
  // arrays. Integer inputs make the contraction and the reference comparison
  // exact.
  std::mt19937 generator(42);
  for (const std::size_t node_count : {3, 7, 16, 31, 64}) {
    for (int repetition = 0; repetition < 8; ++repetition) {
      std::vector<std::int64_t> ordered_parents(node_count, -1);
      for (std::size_t child = 1; child < node_count; ++child) {
        std::uniform_int_distribution<std::size_t> parent(0, child - 1);
        ordered_parents[child] =
            static_cast<std::int64_t>(parent(generator));
      }

      std::vector<btrc::Index> permutation(node_count);
      std::iota(permutation.begin(), permutation.end(), btrc::Index{0});
      std::shuffle(permutation.begin(), permutation.end(), generator);
      std::vector<btrc::Index> inverse(node_count);
      for (std::size_t new_node = 0; new_node < node_count; ++new_node)
        inverse[permutation[new_node]] = static_cast<btrc::Index>(new_node);

      std::vector<std::int64_t> parents(node_count, -1);
      std::vector<int> values(node_count);
      std::vector<int> expected_ordered(node_count);
      for (std::size_t old_node = 0; old_node < node_count; ++old_node) {
        const btrc::Index new_node = inverse[old_node];
        values[new_node] = static_cast<int>(old_node % 11) - 5;
        if (old_node != 0) {
          parents[new_node] = inverse[static_cast<std::size_t>(
              ordered_parents[old_node])];
        }
      }
      for (std::size_t old_node = node_count; old_node-- > 0;) {
        expected_ordered[old_node] += values[inverse[old_node]];
        if (old_node != 0) {
          expected_ordered[static_cast<std::size_t>(
              ordered_parents[old_node])] += expected_ordered[old_node];
        }
      }

      const btrc::Plan plan = btrc::MakePlan(parents);
      SubtreeSumDispatcher dispatcher(plan, values);
      btrc::Contract(plan, dispatcher);
      Check(dispatcher.Root(plan.root()) == expected_ordered[0]);
      dispatcher.SeedRoot(plan.root());
      btrc::Expand(plan, dispatcher);
      for (std::size_t old_node = 0; old_node < node_count; ++old_node)
        Check(dispatcher.outputs()[inverse[old_node]] ==
              expected_ordered[old_node]);
    }
  }
}
