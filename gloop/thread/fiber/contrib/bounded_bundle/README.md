# BoundedBundle

`BoundedBundle` is a wrapper of
[`thread::Bundle`](https://github.com/abseil/gloop/tree/main/gloop/thread/fiber/bundle.h;rcl=9071031664)
that limits the number of concurrently live fibers by blocking an Add() call if
necessary. See
[`bounded_bundle.h`](https://github.com/abseil/gloop/tree/main/gloop/thread/fiber/contrib/bounded_bundle/bounded_bundle.h)
for details.
