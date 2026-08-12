# Bidirectional Tree Rake–Compress

Native, topology-independent planning and bidirectional execution for
rake–compress tree contraction. The package is the reusable layer shared by
CUDA, Metal, and CPU numerical applications; it contains no HMM,
phylogenetics, or optimal-control algebra.

The public split is deliberate:

```text
parent array -> Plan -> generic traversal -> application dispatcher
                                       |-> CPU loops
                                       |-> CUDA kernels
                                       `-> Metal pipelines
```

A dispatcher supplies batched rake, sibling-combination, absorption,
compression, and recovery operations. `btrc::Contract`, `btrc::Expand`, and
`btrc::Reverse` own the traversal order. The last operation reverses the
complete numerical contraction, including sibling reductions, and is intended
for gradients of scalar reductions such as log likelihoods.

The initial implementation includes the deterministic rake–compress planner,
order-preserving balanced sibling reductions, generic forward/recovery/reverse
traversals, and CPU tests. The next scheduler milestone is to port the
readiness-weighted dependency-level representation from
[`jax-bidirectional-tree-rake-compress`](https://github.com/joaospinto/jax_bidirectional_tree_rake_compress),
then attach CUDA and Metal dispatchers without moving numerical algebras into
this repository.

## Build

```sh
bazel test //...
```

The project uses Bazel modules and C++20. CUDA and Metal targets will remain
separate platform-compatible targets so ordinary host tests do not require an
accelerator toolchain.

## License

MIT
