ArenaMP RENDER052 R4.1 cumulative
Baseline: e550d6988cbc3d9d1102e06a6a1bde11565b42b9
Checkpoint: eba88c2611a73fbd73da7dbbbb697e7fbe0c4caf

Этот пакет накладывается самостоятельно на чистый AMP(6) / e550d698.
Он включает R1-R4, HOTFIX1, дальнейший R4 post-processing backport,
а также R4.1:
- восстановлен канонический settings-default.cfg из рабочего файла пользователя;
- antialias alpha test и остальные light/shader keys снова находятся в [Shaders];
- [Post Processing] и per-effect sections расположены корректно;
- исправлен Windows dedicated-server crash при PlayerCellState/instance-cell:
  PlayerPacketController теперь инициализируется и проверяется перед GetPacket().

Старые ArenaMP HDR/Bloom/SMAA/native-effects/PBR-water не возвращаются.
F3 остаётся master toggle нового Post Processing.

Предпочтительный способ: git apply ARENA_RENDER052_R4_1_CUMULATIVE.patch
DELETE_FILES.txt содержит файлы, которые cumulative patch удаляет.
