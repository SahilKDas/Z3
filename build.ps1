$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (!$env:VULKAN_SDK) { throw 'Set VULKAN_SDK to your installed Vulkan SDK directory before building.' }
    $taskSdk = $env:VULKAN_SDK
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DVulkan_INCLUDE_DIR=$taskSdk/Include" "-DVulkan_LIBRARY=$taskSdk/Lib/vulkan-1.lib" "-DVulkan_GLSLC_EXECUTABLE=$taskSdk/Bin/glslc.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
    cmake --build build -j 2
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    ctest --test-dir build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Simulation tests failed' }
    & ./tools/check-size.ps1
} finally { Pop-Location }
