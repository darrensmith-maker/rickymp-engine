# ArenaMP RENDER052 R2 — отдельная ветка переноса OpenMW 0.52

База ArenaMP: `e550d6988cbc3d9d1102e06a6a1bde11565b42b9`.
Донор рендера: загруженный `openmw-master(2).zip`, OpenMW 0.52.0.
Стандарт C++ ArenaMP сохранён: C++17.

## Что уже вырезано в R2

- `NativeEffectsProcessor` (`nativeeffects.cpp/.hpp`) и его world-depth bridge из first-person render bin.
- Встроенный ArenaMP `BloomProcessor` и его отдельные камеры/FBO/callbacks.
- Старые bloom shaders: `bloom_extract_horizontal.frag`, `bloom_vertical.frag`, `bloom_composite.frag`.
- Старый native SMAA: `native_smaa_edge.frag`, `native_smaa_weights.frag`.
- Старый финальный native post shader: `native_final.frag`.
- Старые HDR uniforms/update logic и HDR/Bloom ветка в `lighting.glsl`.
- `HDR.glsl`; `preLight()`/`toneMap()` оставлены как нейтральные migration hooks до нового PostProcessor.
- UI-разделы старых `PBR`, `HDR`, `Bloom`, `Effects` и старые SMAA controls.
- Старый кастомный PBR-water путь и `water_pbr_*` shaders. Старые профили `shader mode = new` мигрируют в `simple`.
- Старые HDR/Bloom/SMAA/native-effects/PBR-lighting настройки удалены из defaults.

## Что добавлено

- Единственный новый владелец post-processing control path: `MWRender::PostProcessor`.
- `[Post Processing]` остаётся встроенным прямо в существующие настройки ArenaMP.
- `F3` горячо переключает `[Post Processing] enabled` для всей будущей цепочки.
- Сканирование VFS на `.omwfx`, построение списка доступных эффектов и чтение `chain`.
- Перенесены donor `.omwfx`: `bloomlinear`, `adjustments`, `debug`, `internal_distortion`.
- Начат C++17-backport `components/fx`: перенесены `lexer.*` и `lexertypes.hpp`, удалена зависимость от C++20 `std::format`/нового strings API.

## Важный переходный момент

R2 уже удаляет конкурирующий старый post-processing, но новый `PostProcessor` пока работает в безопасном passthrough-режиме. Он ещё не перехватывает framebuffer. Это сделано специально, чтобы первая отдельная ветка компилировалась/запускалась без чёрного кадра до переноса полного framebuffer pipeline.

Следующий слой R3 должен добавить за этим же API:

1. HDR scene color target;
2. depth + normals targets;
3. ping-pong canvases;
4. реальный parser/Technique/Pass `.omwfx`;
5. ordered chain execution;
6. transparent postpass;
7. luminance/auto exposure;
8. distortion/refraction integration;
9. Android/NG-GL4ES fallbacks для неподдерживаемых форматов/проходов.

## Что пока физически остаётся в shader tree

`lighting_enhanced_pbr.glsl` и часть совместимого object/terrain material-кода пока остаются как зависимость текущих 0.47 object/terrain shader templates, но Enhanced PBR master принудительно выключен, PBR UI удалён, а material level 4 больше не активируется. Эти исходники удаляются окончательно в том же слое, где object/terrain/groundcover shaders заменяются на 0.52, чтобы не получить промежуточно некомпилируемые shaders.

## Проверки перед упаковкой

- `git diff --check` — OK.
- `openmw_settings_window.layout` — XML parse OK.
- `components/fx/lexer.cpp` — отдельно компилируется `g++ -std=c++17 -I.` в этой среде.
- Проверены локальные quoted includes изменённых C/C++ файлов — отсутствующих файлов нет.
- Полная MSVC/Android сборка в этой среде не выполнялась: это compile-checkpoint для отдельной ветки.

Если сборка упадёт, присылай полный лог с первого compile error. Исправления можно вести последовательно поверх этой ветки, не возвращая удалённые legacy HDR/Bloom/SMAA/PBR-postprocess системы.
