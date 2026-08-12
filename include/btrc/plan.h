#ifndef BTRC_PLAN_H_
#define BTRC_PLAN_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace btrc {

using Index = std::uint32_t;

struct Rake {
  Index edge;
  Index parent;
  Index leaf;
  Index branch;
};

struct BranchCombination {
  Index destination;
  Index source;
  Index tape = 0;
};

struct BranchAbsorption {
  Index parent;
  Index branch;
  Index tape = 0;
};

struct Compression {
  Index middle;
  Index left_edge;
  Index right_edge;
  Index parent;
  Index child;
  Index tape = 0;
};

struct Round {
  std::vector<Rake> rakes;
  std::vector<std::vector<BranchCombination>> branch_reduction_stages;
  std::vector<BranchAbsorption> branch_absorptions;
  std::vector<Compression> compressions;
};

// Earliest-start execution layer. All operations in one level read values
// produced by earlier levels and have conflict-free writes.
struct DependencyLevel {
  std::vector<Rake> rakes;
  std::vector<BranchCombination> branch_combinations;
  std::vector<BranchAbsorption> branch_absorptions;
  std::vector<Compression> compressions;
};

struct PlanStatistics {
  std::size_t nodes = 0;
  std::size_t edges = 0;
  std::size_t rounds = 0;
  std::size_t rakes = 0;
  std::size_t compressions = 0;
  std::size_t primitive_levels = 0;
};

class Plan {
public:
  Index root() const { return root_; }
  std::size_t num_nodes() const { return parents_.size(); }
  std::size_t num_edges() const { return edge_children_.size(); }
  std::size_t num_branches() const { return num_branches_; }
  std::size_t num_branch_combinations() const {
    return num_branch_combinations_;
  }
  std::size_t num_branch_absorptions() const {
    return num_branch_absorptions_;
  }
  std::size_t num_compressions() const { return num_compressions_; }

  std::span<const std::int64_t> parents() const { return parents_; }
  std::span<const Index> edge_parents() const { return edge_parents_; }
  std::span<const Index> edge_children() const { return edge_children_; }
  std::span<const Round> rounds() const { return rounds_; }
  bool uses_dependency_levels() const { return uses_dependency_levels_; }
  std::span<const DependencyLevel> dependency_levels() const {
    return dependency_levels_;
  }

private:
  friend Plan MakePlan(std::span<const std::int64_t>, std::optional<Index>);

  std::vector<std::int64_t> parents_;
  std::vector<Index> edge_parents_;
  std::vector<Index> edge_children_;
  std::vector<Round> rounds_;
  std::vector<DependencyLevel> dependency_levels_;
  Index root_ = 0;
  std::size_t num_branches_ = 0;
  std::size_t num_branch_combinations_ = 0;
  std::size_t num_branch_absorptions_ = 0;
  std::size_t num_compressions_ = 0;
  bool uses_dependency_levels_ = false;
};

// Planning is topology-only and deterministic. A negative or self parent marks
// the root unless root is supplied explicitly.
Plan MakePlan(std::span<const std::int64_t> parents,
              std::optional<Index> root = std::nullopt);

PlanStatistics Statistics(const Plan &plan);

} // namespace btrc

#endif // BTRC_PLAN_H_
