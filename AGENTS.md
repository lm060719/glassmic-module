# GlassMic KernelSU
Standalone KernelSU module: offline WebUI, native audio daemon, Zygisk capture backend. No APK or LSPosed dependency.

- Preserve ../io.mo.glassmic.
- User allows device file reads/writes. Ask before UI navigation, device reboot/shutdown or restarting pre-existing Android services/apps. Isolated test processes created by this task may be started/stopped.
- Do not automatically target games or install hooks in running applications. Test engine in an isolated device directory.
- Audio callbacks: no JNI, IPC, heap allocation or blocking locks.
- arm64-v8a; Android 29+; 16 KiB ELF segment alignment.
- WebUI is local to KernelSU. Do not publish to hosted Sites or expose a network listener.
- Build: scripts/build.ps1. Device test: glassmic-selftest.
- Re-index this project with codebase-memory after changes.
- Check official releases/tags or delivery records before allocating public versions. Preserve the delivered module base 0.1.0 and append/increment the fourth revision segment; never reuse a generated revision. Increment versionCode separately. See docs/releases.md.
