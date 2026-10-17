# GOF2HD — build and packaging / сборка и упаковка

[English](#english) | [Русский](#русский)

## English

[Installation and controls](README.md)

- Current architecture and component responsibilities: [ARCHITECTURE.md](ARCHITECTURE.md) (Russian)
- Technical notes and ABI/FMOD debugging history: [GOF2HD_PORT_NOTES.md](GOF2HD_PORT_NOTES.md) (Russian)

### Requirements

- Git and a running Docker installation;
- support for `linux/arm/v7` containers: x86-64 and ARM64 hosts need ARMv7
  emulation through QEMU/binfmt. Docker Desktop includes it by default;
  with Docker Engine on Linux, configure it if missing
  ([Docker documentation](https://docs.docker.com/build/building/multi-platform/#qemu));
- original game files are not required for building;
- access to container image and Debian package repositories when creating the image for the first time.

### Build on your PC with Docker

Run all commands from the project root on your PC:

```sh
bash port/tools/build-docker.sh
```

The script creates or updates the `gof2hd-build:armhf` image and builds the port for ARMv7.
If you get an `exec format error`, check ARMv7/binfmt support in Docker.

#### Build output and rebuilding

The output is saved to `port/run-native/` on your PC: the `gof2hd` executable
and the port libraries.

The game engine and FMOD libraries from the APK are not part of the build output.
Old copies left in `run-native/` by earlier builds are not removed automatically;
the packaging script includes only an explicit list of port files.

To rebuild, run the same command again: only changed source files and their
dependencies are rebuilt. Game resources, saves and logs are left untouched.
For a clean rebuild, delete only `port/run-native/build/`.

The default is two parallel build jobs. To change this:

```sh
GOF_JOBS=6 bash port/tools/build-docker.sh
```

See [build verification](ARCHITECTURE.md#проверка-сборки) in `ARCHITECTURE.md` (Russian)
for the verification command and details.

#### Create a PortMaster package

After building, run this from the project root on your PC:

```sh
docker run --rm --platform linux/arm/v7 \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$(pwd),target=/work" \
  gof2hd-build:armhf python3 port/tools/package-portmaster.py
```

The result is `dist/gof2hd.zip`, ready to copy to your SD card.
The package contains only port files; original APK/OBB files and saves are not included.

## Русский

[Установка и управление](README.ru.md)

- Актуальная архитектура и границы ответственности — в [ARCHITECTURE.md](ARCHITECTURE.md)
- Технический отчёт «как это устроено внутри» и история ABI/FMOD-диагностики — в [GOF2HD_PORT_NOTES.md](GOF2HD_PORT_NOTES.md)

### Требования

- Git и запущенный Docker;
- поддержка контейнеров `linux/arm/v7`: на x86-64 и ARM64 нужна ARMv7-эмуляция
  QEMU/binfmt; Docker Desktop включает её по умолчанию, для Docker Engine
  на Linux её нужно настроить при отсутствии поддержки ([документация Docker](https://docs.docker.com/build/building/multi-platform/#qemu));
- файлы оригинальной игры для сборки не нужны;
- доступ к репозиториям образов и пакетов Debian при первом создании образа.

### Сборка на ПК через Docker

Все команды выполняются из корня проекта на ПК:

```sh
bash port/tools/build-docker.sh
```

Скрипт создаёт или обновляет образ `gof2hd-build:armhf` и собирает порт для ARMv7.
При ошибке `exec format error` проверьте поддержку ARMv7/binfmt в Docker.

#### Результат и повторная сборка

Результат сохраняется на ПК в `port/run-native/`: исполняемый файл `gof2hd`
и библиотеки порта.

Движок и FMOD из APK не входят в результат сборки. Старые копии, оставшиеся
в `run-native/` от прежних сборок, автоматически не удаляются; скрипт упаковки
включает только явно перечисленные файлы самого порта.

Для следующей сборки повторите ту же команду: пересоберутся только изменённые
исходники и их зависимости. Ресурсы, сохранения и логи не затрагиваются.
Для полной пересборки удалите только каталог `port/run-native/build/`.

По умолчанию используются две параллельные задачи. Чтобы изменить их число:

```sh
GOF_JOBS=6 bash port/tools/build-docker.sh
```

Команда и подробности [проверки сборки](ARCHITECTURE.md#проверка-сборки) — в `ARCHITECTURE.md`.

#### Создание пакета PortMaster

После сборки выполните из корня проекта на ПК:

```sh
docker run --rm --platform linux/arm/v7 \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$(pwd),target=/work" \
  gof2hd-build:armhf python3 port/tools/package-portmaster.py
```

Результат — `dist/gof2hd.zip`, готовый для копирования на карту памяти.
Пакет содержит только файлы порта; оригинальные APK/OBB и сохранения в него не входят.
