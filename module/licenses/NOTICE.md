# Third-party components
- Zygisk API 4 header: topjohnwu/zygisk-module-sample, commit 7bb941ac8edfcffd1d23761e401c45ca95409dc1, ISC (see zygisk-ISC.txt). No Zygisk runtime is distributed.
- ShadowHook v2.0.1: ByteDance, MIT (see shadowhook-MIT.txt). Built statically from the original upstream C/assembly sources with this project's CMake configuration; upstream source files are not modified.
- miniaudio 0.11.23: David Reid, dual public domain / MIT No Attribution (see miniaudio.txt).
- KernelSU JS API 3.0.2: KernelSU contributors, Apache-2.0 (see kernelsu-js-Apache-2.0.txt). The vendored package/index.js is unmodified.

Exact source download URLs and SHA-256 verification are in scripts/prepare_deps.py and dependencies.lock.json in the project source.
