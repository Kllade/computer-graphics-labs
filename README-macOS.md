# Сборка на macOS Apple Silicon

Требуются C++20-компилятор Apple Clang, CMake 3.21+ и Vulkan SDK.
Проверенная версия SDK — 1.4.321.0. GLFW, ImGui, vk-bootstrap и VMA
скачиваются автоматически через CMake FetchContent. Для первой сборки нужен интернет.

```bash
git clone https://github.com/Kllade/computer-graphics-labs.git
cd computer-graphics-labs
bash run-macos.sh
```

Скрипт по умолчанию ищет SDK в `$HOME/VulkanSDK/1.4.321.0/macOS`.
Для другого расположения перед запуском задайте:

```bash
export VULKAN_SDK="/path/to/VulkanSDK/version/macOS"
bash run-macos.sh
```

Для сборки без запуска:

```bash
export VULKAN_SDK="$HOME/VulkanSDK/1.4.321.0/macOS"
cmake --preset macos
cmake --build --preset macos
```

Скрипт запуска задаёт переменные MoltenVK и рабочий каталог проекта.
Запуск приложения двойным щелчком не настраивает эти переменные.
В VS Code можно использовать встроенный терминал или расширение CMake Tools
с конфигурацией macos (VULKAN_SDK должен быть задан в окружении).
Личные задачи автора Vulkan: run и настройки отладчика не публикуются.

## Особенности macOS

- Формат глубины выбирается среди поддерживаемых GPU форматов.
- Используются размеры framebuffer и swapchain в пикселях Retina.
- Записывается командный буфер с render pass и очисткой фона.
- Изменение размера окна отключено: обработчик upstream пока не реализован.
- После обновления Vulkan SDK укажите новый VULKAN_SDK.
