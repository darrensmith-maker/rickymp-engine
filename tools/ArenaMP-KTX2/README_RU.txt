ArenaMP / CoreArenaMP Native KTX2 — конвертация текстур
=========================================================

Что изменено в движке
---------------------
NIF менять не нужно.
Если NIF ссылается, например, на:
    textures\tx_wall_01.dds
движок сначала проверяет:
    textures\tx_wall_01.ktx2
и использует его, если файл существует и корректно загружается.
При ошибке KTX2 автоматически используется исходный DDS/TGA/PNG с тем же именем.

Рекомендуемый формат
--------------------
Для универсального набора ресурсов (Windows сейчас + Android в дальнейшем):
    KTX2 + Basis Universal UASTC + Zstd + mipmaps

На Windows универсальная KTX2-текстура транскодируется во время загрузки:
    без alpha -> BC1 / DXT1 (4 bpp)
    с alpha   -> BC3 / DXT5 (8 bpp)
    нет S3TC  -> RGBA8 fallback

BC7 намеренно не используется в этой версии: текущий OpenMW OSG fork имеет
полный рабочий путь размера/mipmap/vertical flip для S3TC/DXT, но не для BPTC/BC7.

Как конвертировать
------------------
Нужны:
1. Khronos KTX-Software с toktx.exe в PATH.
2. ImageMagick с magick.exe в PATH.

PowerShell:
    .\Convert-TexturesToKTX2.ps1 -InputRoot "D:\Morrowind\Data Files\Textures"

Вывод рядом с исходниками (foo.dds -> foo.ktx2), режим Quality используется по умолчанию.

В отдельную папку:
    .\Convert-TexturesToKTX2.ps1 `
        -InputRoot "D:\SourceTextures" `
        -OutputRoot "D:\Morrowind\Data Files\Textures" `
        -Mode Quality

Компактный режим (ETC1S/BasisLZ, меньше размер на диске, больше потери качества):
    .\Convert-TexturesToKTX2.ps1 -InputRoot "D:\Textures" -Mode Compact

Перезапись уже созданных KTX2:
    .\Convert-TexturesToKTX2.ps1 -InputRoot "D:\Textures" -Overwrite

Важно про DDS
-------------
Если исходник уже DXT1/DXT5 DDS, преобразование DDS -> PNG -> UASTC -> BC1/BC3
является повторным lossy-сжатием. Оно работает, но может немного ухудшить мелкие детали.
Для полной перекодировки лучше использовать исходные PNG/TGA/PSD-экспорты, если они есть.
Если исходников нет — используйте Quality/UASTC и визуально проверьте normals/alpha/fine detail.

Не удаляйте DDS сразу
---------------------
Сначала оставьте оригиналы рядом с KTX2. Движок предпочитает KTX2, но умеет автоматически
откатиться к старому файлу. После проверки всего набора оригиналы можно убрать из релизной
сборки, если вы сознательно отказываетесь от fallback.
