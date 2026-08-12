# Architecture contract

The runtime separates three concerns:

1. **Planning** validates a static rooted tree and records conflict-free rake,
   sibling reduction, absorption, and compression batches.
2. **Traversal** visits those batches forward, backward for application
   recovery, or backward for reverse differentiation.
3. **Numerics** live in a dispatcher supplied by a consumer package.

CUDA and Metal therefore specialize payload storage and local kernels, not the
tree scheduler. A dispatcher method receives a complete independent batch;
the CPU implementation loops, while a device implementation launches one
kernel or compute pipeline. No application should reproduce the round loop.

Application expansion and reverse differentiation are distinct. Expansion
recovers eliminated latent assignments or samples from saved conditional
data. Reverse differentiation propagates adjoints through every primitive and
is the robust route from a partition function to all-node and all-edge
marginals.
