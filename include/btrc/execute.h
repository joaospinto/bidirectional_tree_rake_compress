#ifndef BTRC_EXECUTE_H_
#define BTRC_EXECUTE_H_

#include <cstddef>
#include <span>

#include "btrc/plan.h"

namespace btrc {

// Dispatcher methods operate on whole conflict-free batches. A CPU dispatcher
// may loop over a batch; CUDA and Metal dispatchers launch one kernel for it.
// The runtime, rather than each numerical application, owns traversal order.
template <class Dispatcher>
void Contract(const Plan &plan, Dispatcher &dispatcher) {
  if (plan.uses_dependency_levels()) {
    for (const DependencyLevel &level : plan.dependency_levels()) {
      if (!level.rakes.empty())
        dispatcher.Rake(std::span<const btrc::Rake>(level.rakes));
      if (!level.branch_combinations.empty()) {
        dispatcher.CombineBranches(
            std::span<const BranchCombination>(level.branch_combinations));
      }
      if (!level.branch_absorptions.empty()) {
        dispatcher.AbsorbBranches(
            std::span<const BranchAbsorption>(level.branch_absorptions));
      }
      if (!level.compressions.empty()) {
        dispatcher.Compress(std::span<const Compression>(level.compressions));
      }
    }
    return;
  }
  for (const Round &round : plan.rounds()) {
    if (!round.rakes.empty())
      dispatcher.Rake(std::span<const btrc::Rake>(round.rakes));
    for (const auto &stage : round.branch_reduction_stages) {
      if (!stage.empty()) {
        dispatcher.CombineBranches(
            std::span<const BranchCombination>(stage));
      }
    }
    if (!round.branch_absorptions.empty()) {
      dispatcher.AbsorbBranches(
          std::span<const BranchAbsorption>(round.branch_absorptions));
    }
    if (!round.compressions.empty()) {
      dispatcher.Compress(
          std::span<const Compression>(round.compressions));
    }
  }
}

// Application-level recovery: restore one output for every eliminated node.
template <class Dispatcher>
void Expand(const Plan &plan, Dispatcher &dispatcher) {
  if (plan.uses_dependency_levels()) {
    for (std::size_t level_index = plan.dependency_levels().size();
         level_index-- > 0;) {
      const DependencyLevel &level = plan.dependency_levels()[level_index];
      if (!level.compressions.empty()) {
        dispatcher.ExpandCompressions(
            std::span<const Compression>(level.compressions));
      }
      if (!level.rakes.empty())
        dispatcher.ExpandRakes(std::span<const btrc::Rake>(level.rakes));
    }
    return;
  }
  for (std::size_t round_index = plan.rounds().size(); round_index-- > 0;) {
    const Round &round = plan.rounds()[round_index];
    if (!round.compressions.empty()) {
      dispatcher.ExpandCompressions(
          std::span<const Compression>(round.compressions));
    }
    if (!round.rakes.empty())
      dispatcher.ExpandRakes(std::span<const btrc::Rake>(round.rakes));
  }
}

// Reverse differentiation of the contraction itself. This includes branch
// reductions and absorptions, unlike application-level expansion.
template <class Dispatcher>
void Reverse(const Plan &plan, Dispatcher &dispatcher) {
  if (plan.uses_dependency_levels()) {
    for (std::size_t level_index = plan.dependency_levels().size();
         level_index-- > 0;) {
      const DependencyLevel &level = plan.dependency_levels()[level_index];
      if (!level.compressions.empty()) {
        dispatcher.ReverseCompressions(
            std::span<const Compression>(level.compressions));
      }
      if (!level.branch_absorptions.empty()) {
        dispatcher.ReverseAbsorbBranches(
            std::span<const BranchAbsorption>(level.branch_absorptions));
      }
      if (!level.branch_combinations.empty()) {
        dispatcher.ReverseCombineBranches(
            std::span<const BranchCombination>(level.branch_combinations));
      }
      if (!level.rakes.empty())
        dispatcher.ReverseRakes(std::span<const btrc::Rake>(level.rakes));
    }
    return;
  }
  for (std::size_t round_index = plan.rounds().size(); round_index-- > 0;) {
    const Round &round = plan.rounds()[round_index];
    if (!round.compressions.empty()) {
      dispatcher.ReverseCompressions(
          std::span<const Compression>(round.compressions));
    }
    if (!round.branch_absorptions.empty()) {
      dispatcher.ReverseAbsorbBranches(
          std::span<const BranchAbsorption>(round.branch_absorptions));
    }
    for (std::size_t stage_index = round.branch_reduction_stages.size();
         stage_index-- > 0;) {
      const auto &stage = round.branch_reduction_stages[stage_index];
      if (!stage.empty()) {
        dispatcher.ReverseCombineBranches(
            std::span<const BranchCombination>(stage));
      }
    }
    if (!round.rakes.empty())
      dispatcher.ReverseRakes(std::span<const btrc::Rake>(round.rakes));
  }
}

} // namespace btrc

#endif // BTRC_EXECUTE_H_
