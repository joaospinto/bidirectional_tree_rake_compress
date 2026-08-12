#include "btrc/execute.h"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

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
  const btrc::Plan plan =
      btrc::MakePlan(std::vector<std::int64_t>{-1, 0, 0, 1, 1, 3, 2});
  SubtreeSumDispatcher dispatcher(plan, {1, 2, 3, 4, 5, 6, 7});
  btrc::Contract(plan, dispatcher);
  Check(dispatcher.Root(plan.root()) == 28);
  dispatcher.SeedRoot(plan.root());
  btrc::Expand(plan, dispatcher);
  Check((dispatcher.outputs() == std::vector<int>{28, 17, 10, 10, 5, 6, 7}));
}
