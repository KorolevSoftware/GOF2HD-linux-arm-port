# Архитектура Linux-порта GOF2HD

Актуальное описание проекта на 2026-08-11. История диагностики ABI и FMOD
остаётся в GOF2HD_PORT_NOTES.md; этот файл описывает текущий runtime и
границы ответственности.

## Идея

Порт не является классическим приложением с большим предметным доменом. Его
основная архитектурная задача — изолировать Android ARMv7/bionic-движок от
Linux armhf/glibc. Поэтому наиболее подходящая модель — Ports & Adapters с
Anti-Corruption Layer между бинарём игры и Linux.

    SDL / FIFO -> host input -> virtual Android input -> JNI calls -> game engine
         |                              |
         +--------------> SDL window <--+--> GLES overlay

    game engine / FMOD -> bionic ABI shims -> glibc, libmali, SDL audio

## Текущие bounded contexts

### Host runtime — port/host/gof2hd.c

Это composition root и application shell. Он:

- загружает библиотеки и разрешает символы движка;
- повторяет последовательность Android lifecycle;
- создаёт SDL-окно и GL-контекст;
- получает input от SDL и FIFO;
- запускает frame loop;
- передаёт кадр движку и выполняет swap.

Файл сейчас остаётся точкой сборки приложения, а не библиотекой. Основные
независимые host-модули уже вынесены:

- engine_bridge.c/.h — dlopen/dlsym, ABI-типизированные указатели и вызовы
  движка;
- touch_fifo.c/.h — внешний debug touch input с буферизацией неполных строк.
- config.c/.h — разбор позиционных аргументов и host env в HostConfig.

SDL window, controller mapping и frame loop пока остаются в gof2hd.c. Это
следующий кандидат на выделение, если появится вторая игра или второй input
backend.

Не нужно сразу вводить отдельный framework или десятки интерфейсов.

### Virtual Android input — port/host/wrap_overlay.c/.h

wrap_overlay — это не обычный UI overlay. Это адаптер виртуального Android
touch/accelerometer input, который дополнительно рисует отладочный курсор.

Он владеет:

- режимом cursor/gyro;
- положением виртуального пальца;
- удерживаемыми кнопками;
- преобразованием удержаний в tap/move/fire/back;
- текущим вектором акселерометра;
- отрисовкой курсора в тот же GL-кадр.

Проверенные gyro-mapping и порядок отрисовки перед SDL_GL_SwapWindow являются
частью рабочего контракта и не должны меняться без проверки на устройстве.

Если появятся другие игры с другим input mapping, из этого файла можно будет
вынести чистый virtual_input.c/.h. Пока это было бы преждевременным
раздроблением.

### Fake JNI — port/host/jni.c/.h

Это compatibility adapter, а не часть домена игры. Он предоставляет бинарю:

- минимальные типы JNI;
- fake JNIEnv и JavaVM;
- строки, классы и method IDs;
- значения Android-информации об устройстве.

ABI-описания JNI должны оставаться изолированными от SDL и input. Если другой
игре понадобится другой набор Java-вызовов, расширяется именно этот adapter.

### Bionic-to-glibc ACL — port/shim/

libc.so не является заменой всей bionic libc. Он оставляет только те символы,
где важны имя, версия или layout структуры:

- bionic FILE и __sF;
- stat/statfs и 64-битные file offsets;
- bionic-sized pthread/condition/sem/attr handles;
- mktime и специальные ABI-функции;
- @LIBC version node.

Остальные обычные функции после patch-versions.py резолвятся напрямую в glibc.
Это правильная граница: не нужно вручную форвардить весь libc.

shim/abi.c и fmodex-stub/pthread_bionic.c намеренно не объединены:

- abi.c обслуживает versioned imports самого движка через libc.so;
- pthread_bionic.c перехватывает unversioned вызовы Android FMOD через
  LD_PRELOAD.

Они решают похожую задачу в разных linker scopes. Объединять их можно только
после появления отдельного теста на загрузку движка и FMOD одновременно.

### Math ABI — port/shim/libm.c

libm.so — отдельный softfp-to-hardfp bridge. Он нужен и движку, и FMOD,
поэтому загружается через LD_PRELOAD. Каждая функция должна иметь явный
softfp вход и hardfp function pointer на glibc libm.

### GLES bridge — port/gles-stub/gles-bridge.c

Это ABI adapter для GLES2:

    softfp game / host call
            | pcs("aapcs")
            v
    libGLESv2.so bridge
            | pcs("aapcs-vfp")
            v
    libmali

EGL и создание контекста остаются ответственностью SDL/платформы. В движок
EGL-код не добавляется.

### FMOD compatibility — port/fmodex-stub/

В runtime используются реальные libfmodex.so и libfmodevent.so из APK.
Папка содержит не FMOD-заглушки, а Linux compatibility helpers:

- libOpenSLES.c — OpenSL ES ABI и PCM -> SDL audio;
- fmod_filesystem.c — POSIX filesystem callbacks для FEV/FSB;
- pthread_bionic.c — bionic pthread/sem layout adapter;
- cpuinfo_fake.c — ожидаемые FMOD CPU features;
- libstdcxx_stub.c — Android C++ ABI symbols.

Старые экспериментальные fmodex_stub.c и fmodevent_stub.cpp удалены из
текущего дерева и текущей сборкой не используются.

## Где хранить константы

Минимальное разделение без усложнения:

    port/host/host_config.h

Для host/device-конфигурации:

- /tmp/gof2hd_touch;
- имя ANBERNIC-keys и GUID;
- raw button/axis indexes;
- deadzone и скорость курсора;
- имена env-переменных.

Контракт конкретной Android-игры пока остаётся внутри wrap_overlay и
engine_bridge. Если появится вторая игра, его можно вынести в отдельный
game_contract.h. Для другой консоли сейчас меняется host_config.h и SDL
mapping.

## Где хранить состояние

Не нужен глобальный универсальный singleton. При первом небольшом рефакторинге
достаточно ввести один runtime-контекст:

    typedef struct HostRuntime {
        EngineBridge engine;
        InputBackend input;
        SDL_Window* window;
        SDL_GLContext gl;
        int width;
        int height;
    } HostRuntime;

Состояние виртуального Android input уже правильно локализовано внутри
wrap_overlay по аналогии с этим подходом. Следующий шаг — локализовать
глобальные p_*, g_pad, g_win, g_cls в соответствующие объекты, но не
переписывать рабочую input-логику.

## FIFO touch input

FIFO нужен как внешний injection/debug channel: отдельный процесс может
послать игре touch-событие, например чтобы закрыть logo или проверить меню
без физического контроллера. Это не основной input path.

FIFO уже вынесен в touch_fifo.c/.h. Реализация оставляет только:

1. open FIFO один раз;
2. буфер остатка между read();
3. разбор полных строк pid action x y;
4. выдачу готовых touch commands.

Сейчас FIFO обходит wrap_overlay и вызывает JNI напрямую. Это нормально для
диагностического канала, но в будущем лучше направлять FIFO и SDL в один
InputCommand pipeline.

## Порядок рефакторинга для повторного использования

1. host_config.h, engine_bridge.c/.h и touch_fifo.c/.h уже выделены без
   изменения wrap_overlay.
2. Вынести SDL mapping/backend.
3. Если появится вторая игра, вынести game_contract.h и чистый virtual input
   core.
4. Только после этого рассматривать более крупное разделение host runtime.
5. Не менять рабочие ABI bridges без device regression test.

## Проверка runtime-контрактов

После каждого изменения проверять:

- загрузку движка и JNI lifecycle;
- рендер logo и меню;
- cursor mode и gyro mode;
- tap/back/fire;
- музыку, эффекты и диалоги;
- отсутствие изменения GL-кадра после overlay;
- отсутствие смешивания @LIBC shim и glibc FILE/pthread объектов.
