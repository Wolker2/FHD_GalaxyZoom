# FHD_GalaxyZoom 0.1.5 ALPHA — AModLite MarkerFix

Оригинальный архив: [FHD_GalaxyZoom_v0.1.5_ALPHA_AModLite_MarkerFix.zip](install/FHD_GalaxyZoom_v0.1.5_ALPHA_AModLite_MarkerFix.zip), SHA-256 `0c6a9aa41972825c6cbc190e0ff8bd0b5988f76264c6af6e0ed617b9811f832a`.

Эта папка подготовлена отдельно от ранее опубликованных версий. В исходном архиве 13 файлов, включая Main.dat, готовую DLL, ModuleInfo и README, а также 9 исходных файлов в каталоге SOURCE.

- [Исходники (C, CFG, Python, DEF, SH)](SOURCE)
- [Манифест SHA-256](SHA256SUMS.txt)
- [Список файлов и размеров](SOURCE_FILES.tsv)
- [Отчёт проверки](VALIDATION_REPORT.json)

Исходники переносятся побайтно из ZIP через GitHub Actions. Дополнительно сохраняются оригинальные ModuleInfo.txt и README_RU.txt.

**Проверка:** исходный `SOURCE/build_dat.py` успешно восстанавливает `CFG/Main.dat` из `SOURCE/CFG/Main.txt` побайтно при сохранении оригинальной структуры каталогов. DLL не пересобиралась; при изменении C-кода требуется её отдельная сборка. Архив и игровой код не изменены. Проверка работы в игре не проводилась. Статус — ALPHA / TEST, не RELEASE.

Совместимость с AModGalaxyLite и поведение нового/старого EXE требуют игрового теста. Предыдущие версии репозитория не менялись.
