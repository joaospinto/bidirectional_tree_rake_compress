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

The implementation includes the deterministic rake–compress planner,
order-preserving readiness-weighted sibling reductions, generic
forward/recovery/reverse traversals, adversarial depth tests, and the adaptive
choice between structural rounds and earliest-start dependency levels. A plan
also exposes one flattened sequence of conflict-free primitive batches, so CPU,
CUDA, and Metal consumers share the scheduler's representation instead of
reconstructing it in each numerical package.

## Build

```sh
bazel test //...
```

The project uses Bazel modules and C++20. Accelerator kernels remain in the
numerical packages: this repository owns only topology planning and traversal.

## License

MIT
