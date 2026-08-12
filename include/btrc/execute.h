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
  for (const PrimitiveBatch &batch : plan.primitive_batches()) {
    switch (batch.primitive) {
    case Primitive::kRake:
      dispatcher.Rake(plan.rakes().subspan(batch.offset, batch.count));
      break;
    case Primitive::kBranchCombination:
      dispatcher.CombineBranches(
          plan.branch_combinations().subspan(batch.offset, batch.count));
      break;
    case Primitive::kBranchAbsorption:
      dispatcher.AbsorbBranches(
          plan.branch_absorptions().subspan(batch.offset, batch.count));
      break;
    case Primitive::kCompression:
      dispatcher.Compress(
          plan.compressions().subspan(batch.offset, batch.count));
      break;
    }
  }
}

// Application-level recovery: restore one output for every eliminated node.
template <class Dispatcher>
void Expand(const Plan &plan, Dispatcher &dispatcher) {
  for (std::size_t index = plan.primitive_batches().size(); index-- > 0;) {
    const PrimitiveBatch &batch = plan.primitive_batches()[index];
    switch (batch.primitive) {
    case Primitive::kRake:
      dispatcher.ExpandRakes(plan.rakes().subspan(batch.offset, batch.count));
      break;
    case Primitive::kCompression:
      dispatcher.ExpandCompressions(
          plan.compressions().subspan(batch.offset, batch.count));
      break;
    case Primitive::kBranchCombination:
    case Primitive::kBranchAbsorption:
      break;
    }
  }
}

// Reverse differentiation of the contraction itself. This includes branch
// reductions and absorptions, unlike application-level expansion.
template <class Dispatcher>
void Reverse(const Plan &plan, Dispatcher &dispatcher) {
  for (std::size_t index = plan.primitive_batches().size(); index-- > 0;) {
    const PrimitiveBatch &batch = plan.primitive_batches()[index];
    switch (batch.primitive) {
    case Primitive::kRake:
      dispatcher.ReverseRakes(plan.rakes().subspan(batch.offset, batch.count));
      break;
    case Primitive::kBranchCombination:
      dispatcher.ReverseCombineBranches(
          plan.branch_combinations().subspan(batch.offset, batch.count));
      break;
    case Primitive::kBranchAbsorption:
      dispatcher.ReverseAbsorbBranches(
          plan.branch_absorptions().subspan(batch.offset, batch.count));
      break;
    case Primitive::kCompression:
      dispatcher.ReverseCompressions(
          plan.compressions().subspan(batch.offset, batch.count));
      break;
    }
  }
}

} // namespace btrc

#endif // BTRC_EXECUTE_H_
