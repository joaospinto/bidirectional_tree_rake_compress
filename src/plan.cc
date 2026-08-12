#include "btrc/plan.h"

#include <algorithm>
#include <bit>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace btrc {
namespace {

Index CheckedIndex(std::size_t value, const char *description) {
  if (value > std::numeric_limits<Index>::max())
    throw std::length_error(std::string(description) + " exceeds uint32_t");
  return static_cast<Index>(value);
}

struct NormalizedTree {
  std::vector<std::int64_t> parents;
  Index root;
};

// Minimal unsigned integer used only while planning readiness-weighted
// reductions. Producer weights are powers of two and may exceed machine-word
// range even though the final schedule indices do not.
class BigUInt {
public:
  void AddPower(std::size_t exponent) {
    const std::size_t word = exponent / 64;
    const unsigned shift = static_cast<unsigned>(exponent % 64);
    if (words_.size() <= word)
      words_.resize(word + 1, 0);
    std::uint64_t addend = std::uint64_t{1} << shift;
    std::size_t index = word;
    while (addend != 0) {
      if (index == words_.size())
        words_.push_back(0);
      const std::uint64_t previous = words_[index];
      words_[index] += addend;
      addend = words_[index] < previous ? 1 : 0;
      ++index;
    }
  }

  void ShiftLeftOne() {
    std::uint64_t carry = 0;
    for (std::uint64_t &word : words_) {
      const std::uint64_t next = word >> 63;
      word = (word << 1) | carry;
      carry = next;
    }
    if (carry != 0)
      words_.push_back(carry);
  }

  int Compare(const BigUInt &other) const {
    const std::size_t left_size = SignificantWords();
    const std::size_t right_size = other.SignificantWords();
    if (left_size != right_size)
      return left_size < right_size ? -1 : 1;
    for (std::size_t index = left_size; index-- > 0;) {
      if (words_[index] != other.words_[index])
        return words_[index] < other.words_[index] ? -1 : 1;
    }
    return 0;
  }

  void Subtract(const BigUInt &other) {
    if (Compare(other) < 0)
      throw std::logic_error("negative BigUInt subtraction");
    std::uint64_t borrow = 0;
    for (std::size_t index = 0; index < words_.size(); ++index) {
      const std::uint64_t right =
          index < other.words_.size() ? other.words_[index] : 0;
      const std::uint64_t with_borrow = right + borrow;
      const bool addition_overflow = with_borrow < right;
      const std::uint64_t left = words_[index];
      words_[index] = left - with_borrow;
      borrow = (addition_overflow || left < with_borrow) ? 1 : 0;
    }
    if (borrow != 0)
      throw std::logic_error("BigUInt subtraction underflow");
    Trim();
  }

  std::size_t BitLength() const {
    const std::size_t size = SignificantWords();
    if (size == 0)
      return 0;
    return 64 * (size - 1) + std::bit_width(words_[size - 1]);
  }

  bool IsPowerOfTwo() const {
    bool found = false;
    for (const std::uint64_t word : words_) {
      if (word == 0)
        continue;
      if (found || (word & (word - 1)) != 0)
        return false;
      found = true;
    }
    return found;
  }

private:
  std::size_t SignificantWords() const {
    std::size_t size = words_.size();
    while (size > 0 && words_[size - 1] == 0)
      --size;
    return size;
  }

  void Trim() { words_.resize(SignificantWords()); }

  std::vector<std::uint64_t> words_;
};

struct CodeTrie {
  std::unique_ptr<CodeTrie> zero;
  std::unique_ptr<CodeTrie> one;
  std::optional<std::size_t> leaf;
};

std::size_t SynchronousLevels(std::span<const Round> rounds) {
  std::size_t result = 0;
  for (const Round &round : rounds) {
    result += round.rakes.empty() ? 0 : 1;
    result += round.branch_reduction_stages.size();
    result += round.branch_absorptions.empty() ? 0 : 1;
    result += round.compressions.empty() ? 0 : 1;
  }
  return result;
}

template <class Operation>
void AppendBatch(std::vector<Operation> &destination,
                 std::span<const Operation> operations, Primitive primitive,
                 std::vector<PrimitiveBatch> &batches) {
  if (operations.empty())
    return;
  const Index offset = CheckedIndex(destination.size(), "operation offset");
  destination.insert(destination.end(), operations.begin(), operations.end());
  batches.push_back(
      {primitive, offset, CheckedIndex(operations.size(), "operation count")});
}

std::vector<DependencyLevel>
BuildDependencyLevels(std::size_t node_count, std::size_t edge_count,
                      std::size_t branch_count,
                      std::span<const Round> rounds) {
  std::vector<int> node_producers(node_count, -1);
  std::vector<int> path_producers(edge_count, -1);
  std::vector<int> branch_producers(branch_count, -1);
  std::vector<DependencyLevel> levels;

  const auto ensure_level = [&levels](int level) -> DependencyLevel & {
    if (level < 0)
      throw std::logic_error("negative dependency level");
    while (levels.size() <= static_cast<std::size_t>(level))
      levels.emplace_back();
    return levels[static_cast<std::size_t>(level)];
  };

  for (const Round &round : rounds) {
    for (const Rake &rake : round.rakes) {
      const int level =
          1 + std::max(path_producers[rake.edge], node_producers[rake.leaf]);
      branch_producers[rake.branch] = level;
      ensure_level(level).rakes.push_back(rake);
    }

    std::map<Index, std::vector<Index>> groups;
    for (const Rake &rake : round.rakes)
      groups[rake.parent].push_back(rake.branch);

    for (const auto &[parent, branches] : groups) {
      BigUInt total;
      for (const Index branch : branches) {
        const int producer = branch_producers[branch];
        if (producer < 0)
          throw std::logic_error("branch has no producer");
        total.AddPower(static_cast<std::size_t>(producer));
      }

      CodeTrie trie;
      BigUInt cumulative;
      for (std::size_t branch_index = 0; branch_index < branches.size();
           ++branch_index) {
        const int producer = branch_producers[branches[branch_index]];
        const std::size_t exponent = static_cast<std::size_t>(producer);
        const std::size_t ceil_log_total =
            total.BitLength() - (total.IsPowerOfTwo() ? 1 : 0);
        if (ceil_log_total < exponent)
          throw std::logic_error("invalid readiness weight");
        const std::size_t code_length = ceil_log_total - exponent + 1;

        // Binary expansion of the midpoint of this contribution's interval:
        // (2*cumulative + weight) / (2*total). Dyadic midpoints receive
        // trailing zero bits, matching the alphabetic SFE construction.
        BigUInt numerator = cumulative;
        numerator.ShiftLeftOne();
        numerator.AddPower(exponent);
        BigUInt denominator = total;
        denominator.ShiftLeftOne();

        CodeTrie *node = &trie;
        for (std::size_t bit_index = 0; bit_index < code_length; ++bit_index) {
          numerator.ShiftLeftOne();
          const bool bit = numerator.Compare(denominator) >= 0;
          if (bit)
            numerator.Subtract(denominator);
          std::unique_ptr<CodeTrie> &child = bit ? node->one : node->zero;
          if (!child)
            child = std::make_unique<CodeTrie>();
          node = child.get();
        }
        if (node->leaf.has_value() || node->zero || node->one)
          throw std::logic_error("Shannon--Fano--Elias code is not prefix-free");
        node->leaf = branch_index;
        cumulative.AddPower(exponent);
      }

      std::function<Index(CodeTrie &)> reduce = [&](CodeTrie &node) -> Index {
        if (node.leaf.has_value())
          return branches[*node.leaf];
        if (!node.zero && !node.one)
          throw std::logic_error("empty reduction trie");
        if (!node.zero)
          return reduce(*node.one);
        if (!node.one)
          return reduce(*node.zero);
        const Index destination = reduce(*node.zero);
        const Index source = reduce(*node.one);
        const int level =
            1 + std::max(branch_producers[destination],
                         branch_producers[source]);
        branch_producers[destination] = level;
        ensure_level(level).branch_combinations.push_back(
            {destination, source});
        return destination;
      };

      const Index root_branch = reduce(trie);
      const int absorption_level =
          1 + std::max(branch_producers[root_branch], node_producers[parent]);
      node_producers[parent] = absorption_level;
      ensure_level(absorption_level)
          .branch_absorptions.push_back({parent, root_branch});
    }

    for (const Compression &compression : round.compressions) {
      const int level =
          1 + std::max({node_producers[compression.middle],
                        path_producers[compression.left_edge],
                        path_producers[compression.right_edge]});
      path_producers[compression.left_edge] = level;
      ensure_level(level).compressions.push_back(compression);
    }
  }
  return levels;
}

NormalizedTree Normalize(std::span<const std::int64_t> input,
                         std::optional<Index> requested_root) {
  if (input.empty())
    throw std::invalid_argument("a tree must contain at least one node");
  CheckedIndex(input.size() - 1, "node index");

  std::vector<std::int64_t> parents(input.begin(), input.end());
  std::vector<Index> candidates;
  for (std::size_t node = 0; node < parents.size(); ++node) {
    if (parents[node] < 0 || parents[node] == static_cast<std::int64_t>(node))
      candidates.push_back(CheckedIndex(node, "root index"));
  }

  Index root = 0;
  if (requested_root.has_value()) {
    root = *requested_root;
    if (root >= parents.size())
      throw std::invalid_argument("root is outside the node array");
  } else {
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());
    if (candidates.size() != 1) {
      throw std::invalid_argument(
          "parents must identify exactly one negative or self-parent root");
    }
    root = candidates.front();
  }

  std::vector<std::vector<Index>> children(parents.size());
  for (std::size_t node = 0; node < parents.size(); ++node) {
    if (node == root) {
      if (parents[node] >= 0 &&
          parents[node] != static_cast<std::int64_t>(node)) {
        throw std::invalid_argument("the root has a non-root parent");
      }
      continue;
    }
    const std::int64_t parent = parents[node];
    if (parent < 0 || parent >= static_cast<std::int64_t>(parents.size()) ||
        parent == static_cast<std::int64_t>(node)) {
      throw std::invalid_argument("invalid parent for non-root node");
    }
    children[static_cast<std::size_t>(parent)].push_back(
        CheckedIndex(node, "node index"));
  }
  parents[root] = -1;

  std::vector<bool> visited(parents.size(), false);
  std::vector<Index> stack{root};
  std::size_t visited_count = 0;
  while (!stack.empty()) {
    const Index node = stack.back();
    stack.pop_back();
    if (visited[node])
      throw std::invalid_argument("parents contain a directed cycle");
    visited[node] = true;
    ++visited_count;
    stack.insert(stack.end(), children[node].begin(), children[node].end());
  }
  if (visited_count != parents.size())
    throw std::invalid_argument("parents do not describe one rooted tree");
  return {std::move(parents), root};
}

} // namespace

Plan MakePlan(std::span<const std::int64_t> input,
              std::optional<Index> requested_root) {
  NormalizedTree tree = Normalize(input, requested_root);
  Plan plan;
  plan.parents_ = std::move(tree.parents);
  plan.root_ = tree.root;

  const std::size_t node_count = plan.parents_.size();
  for (std::size_t node = 0; node < node_count; ++node) {
    if (node == plan.root_)
      continue;
    plan.edge_children_.push_back(CheckedIndex(node, "edge child"));
    plan.edge_parents_.push_back(
        CheckedIndex(static_cast<std::size_t>(plan.parents_[node]),
                     "edge parent"));
  }

  const Index missing = std::numeric_limits<Index>::max();
  std::vector<Index> incoming(node_count, missing);
  std::vector<Index> active_parent(node_count, missing);
  std::vector<Index> edge_child = plan.edge_children_;
  std::vector<std::set<Index>> active_children(node_count);
  for (std::size_t edge = 0; edge < plan.num_edges(); ++edge) {
    const Index edge_index = CheckedIndex(edge, "edge index");
    const Index parent = plan.edge_parents_[edge];
    const Index child = plan.edge_children_[edge];
    incoming[child] = edge_index;
    active_parent[child] = parent;
    active_children[parent].insert(edge_index);
  }

  std::vector<bool> active(node_count, true);
  std::size_t active_count = node_count;
  while (active_count > 1) {
    Round round;
    for (std::size_t raw_node = 0; raw_node < node_count; ++raw_node) {
      const Index node = CheckedIndex(raw_node, "node index");
      if (active[node] && node != plan.root_ && active_children[node].empty()) {
        round.rakes.push_back(
            {incoming[node], active_parent[node], node,
             CheckedIndex(plan.num_branches_++, "branch slot")});
      }
    }

    for (const Rake &rake : round.rakes) {
      active_children[rake.parent].erase(rake.edge);
      active[rake.leaf] = false;
      --active_count;
    }

    std::vector<Index> selected;
    std::vector<bool> selected_middle(node_count, false);
    for (std::size_t raw_node = 0; raw_node < node_count; ++raw_node) {
      const Index node = CheckedIndex(raw_node, "node index");
      if (!active[node] || node == plan.root_ ||
          active_children[node].size() != 1) {
        continue;
      }
      const Index parent = active_parent[node];
      const Index right_edge = *active_children[node].begin();
      const Index child = edge_child[right_edge];
      if (selected_middle[parent] || selected_middle[child])
        continue;
      selected.push_back(node);
      selected_middle[node] = true;
    }

    for (const Index middle : selected) {
      const Index left_edge = incoming[middle];
      const Index right_edge = *active_children[middle].begin();
      const Index parent = active_parent[middle];
      const Index child = edge_child[right_edge];
      round.compressions.push_back(
          {middle, left_edge, right_edge, parent, child});
    }
    for (const Compression &compression : round.compressions) {
      edge_child[compression.left_edge] = compression.child;
      active_parent[compression.child] = compression.parent;
      incoming[compression.child] = compression.left_edge;
      active[compression.middle] = false;
      --active_count;
    }

    std::map<Index, std::vector<Index>> groups;
    for (const Rake &rake : round.rakes)
      groups[rake.parent].push_back(rake.branch);
    while (std::any_of(groups.begin(), groups.end(), [](const auto &entry) {
      return entry.second.size() > 1;
    })) {
      std::vector<BranchCombination> stage;
      for (auto &[parent, branches] : groups) {
        static_cast<void>(parent);
        std::vector<Index> survivors;
        for (std::size_t index = 0; index + 1 < branches.size(); index += 2) {
          stage.push_back({branches[index], branches[index + 1]});
          survivors.push_back(branches[index]);
        }
        if (branches.size() % 2 != 0)
          survivors.push_back(branches.back());
        branches = std::move(survivors);
      }
      if (!stage.empty())
        round.branch_reduction_stages.push_back(std::move(stage));
    }
    for (const auto &[parent, branches] : groups) {
      if (!branches.empty())
        round.branch_absorptions.push_back({parent, branches.front()});
    }

    if (round.rakes.empty() && round.compressions.empty())
      throw std::logic_error("tree-contraction planning made no progress");
    plan.rounds_.push_back(std::move(round));
  }

  const std::size_t max_reduction_stages =
      std::accumulate(plan.rounds_.begin(), plan.rounds_.end(), std::size_t{0},
                      [](std::size_t maximum, const Round &round) {
                        return std::max(maximum,
                                        round.branch_reduction_stages.size());
                      });
  if (plan.rounds_.size() > 1 && max_reduction_stages > 1) {
    std::vector<DependencyLevel> dependency_levels = BuildDependencyLevels(
        plan.num_nodes(), plan.num_edges(), plan.num_branches_, plan.rounds_);
    const std::size_t synchronous_levels = SynchronousLevels(plan.rounds_);
    // Finer-grained levels need a persistent branch workspace and more
    // launches. Select them only when they remove a nonconstant fraction of
    // the synchronous span. Otherwise the round representation is already
    // within a factor of two of the logarithmic dependency depth.
    if (2 * dependency_levels.size() < synchronous_levels) {
      plan.uses_dependency_levels_ = true;
      plan.dependency_levels_ = std::move(dependency_levels);
    }
  }

  if (plan.uses_dependency_levels_) {
    for (DependencyLevel &level : plan.dependency_levels_) {
      for (BranchCombination &operation : level.branch_combinations) {
        operation.tape = CheckedIndex(plan.num_branch_combinations_++,
                                      "branch-combination tape slot");
      }
      for (BranchAbsorption &operation : level.branch_absorptions) {
        operation.tape = CheckedIndex(plan.num_branch_absorptions_++,
                                      "branch-absorption tape slot");
      }
      for (Compression &operation : level.compressions) {
        operation.tape =
            CheckedIndex(plan.num_compressions_++, "compression tape slot");
      }
    }
  } else {
    for (Round &round : plan.rounds_) {
      for (auto &stage : round.branch_reduction_stages) {
        for (BranchCombination &operation : stage) {
          operation.tape = CheckedIndex(plan.num_branch_combinations_++,
                                        "branch-combination tape slot");
        }
      }
      for (BranchAbsorption &operation : round.branch_absorptions) {
        operation.tape = CheckedIndex(plan.num_branch_absorptions_++,
                                      "branch-absorption tape slot");
      }
      for (Compression &operation : round.compressions) {
        operation.tape =
            CheckedIndex(plan.num_compressions_++, "compression tape slot");
      }
    }
  }

  if (plan.uses_dependency_levels_) {
    for (const DependencyLevel &level : plan.dependency_levels_) {
      AppendBatch(plan.rakes_, std::span<const Rake>(level.rakes),
                  Primitive::kRake, plan.primitive_batches_);
      AppendBatch(plan.branch_combinations_,
                  std::span<const BranchCombination>(
                      level.branch_combinations),
                  Primitive::kBranchCombination, plan.primitive_batches_);
      AppendBatch(plan.branch_absorptions_,
                  std::span<const BranchAbsorption>(level.branch_absorptions),
                  Primitive::kBranchAbsorption, plan.primitive_batches_);
      AppendBatch(plan.compressions_,
                  std::span<const Compression>(level.compressions),
                  Primitive::kCompression, plan.primitive_batches_);
    }
  } else {
    for (const Round &round : plan.rounds_) {
      AppendBatch(plan.rakes_, std::span<const Rake>(round.rakes),
                  Primitive::kRake, plan.primitive_batches_);
      for (const auto &stage : round.branch_reduction_stages) {
        AppendBatch(plan.branch_combinations_,
                    std::span<const BranchCombination>(stage),
                    Primitive::kBranchCombination,
                    plan.primitive_batches_);
      }
      AppendBatch(plan.branch_absorptions_,
                  std::span<const BranchAbsorption>(round.branch_absorptions),
                  Primitive::kBranchAbsorption, plan.primitive_batches_);
      AppendBatch(plan.compressions_,
                  std::span<const Compression>(round.compressions),
                  Primitive::kCompression, plan.primitive_batches_);
    }
  }
  return plan;
}

PlanStatistics Statistics(const Plan &plan) {
  PlanStatistics result;
  result.nodes = plan.num_nodes();
  result.edges = plan.num_edges();
  result.rounds = plan.rounds().size();
  for (const Round &round : plan.rounds()) {
    result.rakes += round.rakes.size();
    result.compressions += round.compressions.size();
  }
  result.primitive_levels = plan.uses_dependency_levels()
                                ? plan.dependency_levels().size()
                                : SynchronousLevels(plan.rounds());
  return result;
}

} // namespace btrc
