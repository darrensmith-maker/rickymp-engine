Исправление ошибки CMake:
  set_target_properties Can not find target to add properties to: openmw

Причина:
  apps/openmw/CMakeLists.txt из предыдущего пакета создавал цель tes3mp,
  а корневой CMakeLists.txt сингл-ветки OpenMW ожидает цель openmw.

Применение:
  1. Скопировать папку apps из этого архива в корень ArenaMW_TTS.
  2. Подтвердить замену apps/openmw/CMakeLists.txt.
  3. Удалить каталог MSVC2022_64_Ninja/CMakeCache.txt и CMakeFiles
     либо запустить workflow заново с чистой конфигурацией.

Изменены только ссылки на CMake-target:
  tes3mp -> openmw

Пути files/tes3mp/tes3mp.rc и tes3mp.exe.manifest не менялись намеренно:
они относятся к ресурсам форка и не являются именем CMake-target.
