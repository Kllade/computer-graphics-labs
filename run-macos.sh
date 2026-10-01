#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
export VULKAN_SDK="${VULKAN_SDK:-$HOME/VulkanSDK/1.4.321.0/macOS}"
if [ ! -f "$VULKAN_SDK/include/vulkan/vulkan.h" ]; then
    echo "Set VULKAN_SDK to the SDK macOS directory." >&2
    exit 1
fi
export VK_DRIVER_FILES="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
export VK_ADD_LAYER_PATH="$VULKAN_SDK/share/vulkan/explicit_layer.d"
cmake --preset macos
cmake --build --preset macos
exec ./build-macos/vulkan-starter-app.app/Contents/MacOS/vulkan-starter-app
