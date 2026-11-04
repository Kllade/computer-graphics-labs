#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
lab="${1:-3}"
if [ "$#" -gt 1 ] || [[ ! "$lab" =~ ^[123]$ ]]; then
    echo "Usage: bash run-macos.sh [1|2|3]" >&2
    exit 2
fi
export VULKAN_SDK="${VULKAN_SDK:-$HOME/VulkanSDK/1.4.321.0/macOS}"
if [ ! -f "$VULKAN_SDK/include/vulkan/vulkan.h" ]; then
    echo "Set VULKAN_SDK to the SDK macOS directory." >&2
    exit 1
fi
export VK_DRIVER_FILES="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
export VK_ADD_LAYER_PATH="$VULKAN_SDK/share/vulkan/explicit_layer.d"
cmake -S . -B build-macos -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Debug \
    "-DLAB_NUMBER=$lab" \
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-macos --parallel 4
exec ./build-macos/vulkan-starter-app.app/Contents/MacOS/vulkan-starter-app
