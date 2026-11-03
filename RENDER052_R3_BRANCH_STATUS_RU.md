# ArenaMP RENDER052 R3 — реальное выполнение OpenMW 0.52 `.omwfx`

R3 ветка создана поверх RENDER052 R2 commit `3ac5e05ed3bd8d7228bbcd75a4aac7d6ff4c9804`.
Текущий R3 checkpoint — HEAD ветки `render052-r3-execution` в полном архиве/патче.
Исходная база ArenaMP: `e550d6988cbc3d9d1102e06a6a1bde11565b42b9`.
Донор: OpenMW 0.52.0 из `openmw-master(2).zip`.
C++ стандарт ArenaMP сохранён: C++17.

## Что изменилось относительно R2

R2 был безопасным passthrough-checkpoint. R3 впервые включает реальную post-processing цепочку:

- добавлен C++17 parser `Fx::Technique` поверх уже перенесённого lexer;
- поддерживаются `uniform_bool/int/float/vec2/vec3/vec4`;
- поддерживаются `shared`, `render_target`, `fragment`, `technique`;
- поддерживаются `passes`, `target=`, `rt1/rt2/rt3`, flags и `pass_normals` metadata;
- parser проверен на всех четырех donor `.omwfx` из текущего дерева;
- `PostProcessor` теперь захватывает scene color + world depth после main scene и до MyGUI;
- восстановлен корректный world-depth bridge перед first-person depth clear;
- создаются реальные FBO pass cameras;
- custom render targets `.omwfx` создаются с ratio/format/filter/wrap параметрами;
- безымянные output-pass'ы идут через два full-resolution ping-pong target;
- `omw_GetLastShader`, `omw_GetLastPass`, `omw_GetDepth`, `omw_GetLinearDepth` уже доступны эффектам;
- `omw.resolution`, `omw.simulationTime`, `omw.near`, `omw.far` обновляются каждый кадр;
- final blit возвращает результат на основной framebuffer перед HUD/MyGUI;
- `F3` по-прежнему мгновенно включает/выключает всю новую цепочку;
- default chain остается `bloomlinear, adjustments`;
- `bloomlinear.omwfx` получил маленькую GLSL-120 compatibility правку: чтение simulationTime перенесено из global initializer внутрь функции.

## Форматы и Android

Desktop R3 пытается использовать floating-point scene/ping targets (RGBA16F, если константы доступны) и donor RGB16F render targets.

На Android/NG-GL4ES R3 **намеренно** делает safe fallback float target -> RGB/RGBA8. Это нужно, чтобы первая реальная `.omwfx` ветка не начиналась с черного кадра на устройствах без надежного half-float FBO. Capability-driven HDR Android path будет следующим слоем.

## Что пока ещё НЕ является полным OpenMW 0.52 renderer

R3 — это реальный postprocess execution core, но не окончание переноса:

- normal MRT пока не подключен (`OMW_NORMALS=0`), поэтому debug normals отключены на уровне shader define;
- distortion texture пока возвращает zero-vector; `internal_distortion` компилируется/парсится, но реальная refraction integration впереди;
- auto exposure/luminance texture пока не используется, хотя настройка сохранена;
- transparent postpass control сохранён, реальный transparent depth/post pass еще впереди;
- compute/vertex/sampler blocks `.omwfx` R3 специально отклоняет с понятной ошибкой вместо неправильного исполнения;
- shader parameter widgets пока используют defaults; полноценное редактирование uniform-параметров прямо в настройках — следующий UI слой;
- object/terrain/groundcover shader transplant 0.52 и окончательное удаление `lighting_enhanced_pbr.glsl` еще впереди.

## Проверки

- `components/fx/lexer.cpp + technique.cpp` отдельно компилируются с `g++ -std=c++17`;
- parser успешно разбирает `adjustments.omwfx`, `bloomlinear.omwfx`, `debug.omwfx`, `internal_distortion.omwfx`;
- `git diff --check` должен проходить перед упаковкой;
- полная MSVC/Android сборка в этой среде недоступна — это compile/runtime checkpoint для твоей отдельной ветки.

Если R2 еще не успел собраться, можно сразу пробовать полный R3 архив отдельной веткой. При ошибке присылай лог **от самого первого compiler error** и примерно 80–150 строк после него.
