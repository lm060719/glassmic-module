param(
    [string]$Sdk = 'E:\Android\Sdk',
    [string]$Ndk = '26.1.10909125',
    [string]$Java = 'C:\Program Files\Java\jdk-17.0.0.1'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$nativeBuild = Join-Path $root 'build\native'
$stage = Join-Path $root 'build\module'
$dist = Join-Path $root 'dist'
$cmake = Join-Path $Sdk 'cmake\3.22.1\bin\cmake.exe'
$ninja = Join-Path $Sdk 'cmake\3.22.1\bin\ninja.exe'
$toolchain = Join-Path $Sdk "ndk\$Ndk\build\cmake\android.toolchain.cmake"
Push-Location $root
try {
    python scripts/prepare_deps.py
    if ($LASTEXITCODE -ne 0) { throw 'Dependency verification failed' }
    & $cmake -S native -B $nativeBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=RelWithDebInfo
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & $cmake --build $nativeBuild -j 8
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
    # Audio callbacks must not use TLS: Android's emulated TLS mallocs and locks
    # on a thread's first access. libc++abi's exception globals are allowed.
    $readelf = Join-Path $Sdk "ndk\$Ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe"
    $tls = & $readelf -lsW (Join-Path $nativeBuild 'libglassmic-backend.so') | Select-String -Pattern '\sTLS\s|__emutls_v\._Z+N2gm'
    if ($LASTEXITCODE -ne 0) { throw 'Backend ELF inspection failed' }
    if ($tls) { throw "Backend uses thread-local storage:`n$($tls -join "`n")" }
    New-Item -ItemType Directory -Path $stage,$dist,(Join-Path $stage 'bin'),(Join-Path $stage 'zygisk'),(Join-Path $stage 'lib'),(Join-Path $root 'build\java') -Force | Out-Null
    Copy-Item -Path (Join-Path $root 'module\*') -Destination $stage -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $stage 'LICENSE') -Force
    foreach ($binary in 'glassmicd','glassmicctl') { Copy-Item -LiteralPath (Join-Path $nativeBuild $binary) -Destination (Join-Path $stage 'bin') -Force }
    Copy-Item -LiteralPath (Join-Path $nativeBuild 'libglassmic.so') -Destination (Join-Path $stage 'zygisk\arm64-v8a.so') -Force
    Copy-Item -LiteralPath (Join-Path $nativeBuild 'libglassmic-backend.so') -Destination (Join-Path $stage 'lib\arm64-v8a.so') -Force
    # ShadowHook matches this exact file name while hooking the linker.
    New-Item -ItemType Directory -Path (Join-Path $stage 'lib\nothing\arm64-v8a') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $nativeBuild 'libshadowhook_nothing.so') -Destination (Join-Path $stage 'lib\nothing\arm64-v8a\libshadowhook_nothing.so') -Force
    & (Join-Path $Java 'bin\javac.exe') -encoding UTF-8 -source 17 -target 17 -cp (Join-Path $Sdk 'platforms\android-35\android.jar') -d (Join-Path $root 'build\java') (Join-Path $root 'tts\Main.java')
    if ($LASTEXITCODE -ne 0) { throw 'TTS Java build failed' }
    & (Join-Path $Java 'bin\jar.exe') cf (Join-Path $root 'build\tts-classes.jar') -C (Join-Path $root 'build\java') .
    $env:JAVA_HOME = $Java
    & (Join-Path $Sdk 'build-tools\35.0.0\d8.bat') --min-api 29 --lib (Join-Path $Sdk 'platforms\android-35\android.jar') --output (Join-Path $stage 'bin\glassmic-tts.jar') (Join-Path $root 'build\tts-classes.jar')
    if ($LASTEXITCODE -ne 0) { throw 'TTS dex build failed' }
    python scripts/package.py
    if ($LASTEXITCODE -ne 0) { throw 'Module packaging failed' }
} finally { Pop-Location }
