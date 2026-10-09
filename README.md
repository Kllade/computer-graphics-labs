# Лабораторная №1: основы 3D-графики

Рахимов Михаил Юрьевич, М8О-301БВ-24. Номер 23 → вариант 11: **правильный икосаэдр**.
Базовая версия без дополнительных заданий: один объект, фиксированные камера,
перспектива и цвет. Стек: C++20, Vulkan, GLFW, ImGui, GLM.

## Запуск на macOS

Нужны Apple Clang, CMake и Vulkan SDK. При первой сборке нужен интернет.
SDK по умолчанию: `$HOME/VulkanSDK/1.4.321.0/macOS`; другой путь задаётся через `VULKAN_SDK`.
Из папки проекта:

```bash
bash run-macos.sh
```

В VS Code: `Terminal → Run Task → Vulkan: run`.

## Код

- `source/icosahedron.hpp` — 12 вершин и 20 треугольных граней.
- `source/application.cpp` — создание ресурсов, интерфейс и команды рисования.
- `shaders/icosahedron.vert` — позиция вершины: `MVP * vec4(position, 1)`.
- `shaders/icosahedron.frag` — синий цвет и фиксированная подсветка граней.
- `source/main.cpp`, `graphics_internal.*` — [шаблон преподавателя](https://github.com/vladeemerr/vulkan-starter-app).

`initialize` создаёт ресурсы; `update` рисует ImGui; `render` передаёт матрицу
`P * V * M` через push constants и рисует 60 индексов; `shutdown` освобождает ресурсы.
Буфер глубины скрывает задние грани. UBO и дескрипторы для фигуры не используются.
Сохранены исправления MoltenVK/Retina; окно фиксировано. Проверено только на macOS.

[План разработки](openspec/changes/lab1-icosahedron/proposal.md).
[Шпаргалка: 30 вопросов для защиты](DEFENSE-LAB1.md).
Отчёт в `reports/` пока относится к прежней полной версии.
