# GOF2HD Linux Port — технический отчёт

Дата: 2026-08-06 (последнее обновление: 2026-08-07)
Проект: порт Galaxy on Fire 2 HD (Android, ARMv7, bionic) на Linux ARM-устройства.

## 0. АКТУАЛЬНОЕ СОСТОЯНИЕ (что нужно знать СЕЙЧАС)

> Разделы 3–12 ниже — история процесса. Свежая картина — здесь. Старые блокеры
> (softfp/hardfp противоречие, бесконечный CPU-цикл в софт-GLES) **решены**:
> игра работает на аппаратном Mali через SDL2-окно + ABI-мост.

### Устройство (эталон)
- **ANBERNIC (стоковая прошивка), root@192.168.0.128, armhf glibc 2.35 + aarch64 kernel.**
- Пути: `/root/gof2hd/` (проект), `/root/gof2hd/port/run-native/` (собранное),
  `/root/gof2hd/port_bak/` (бэкап), `/dev/fb0` (framebuffer), `/dev/input/js0` (геймпад).
- GPU: настоящие EGL/GLES — в `/usr/lib32/libmali.so.0.20.0`. Пустышка
  `/usr/lib32/libEGL.so.1.4.0` (2968 Б, без EGL-символов).
- glibc: **2.35** (проверено `ldd --version`), поэтому `sscanf` резолвится
  как `__isoc99_sscanf@GLIBC_2.7` — обычный `sscanf` использовать можно.

### Видео-путь (работает)
- Хост рисует через **SDL2** (видеодрайвер `mali`, ядро fb0) + EGL.
- SDL грузит наш `libEGL.so.1` из `run-native`, наши EGL-символы форвардятся
  на реальные из `libmali.so` (`gles-stub/gles-bridge.c`, секция EGL_FWD).
- В логе: `SDL window ready (video driver: mali)`, `logo=1` — рендер идёт.

### Геймпад (ввод)
- **Встроенный** геймпад консоли `ANBERNIC-keys` (js0/event1, gpio-keys-polled).
  Виртуальный uinput-геймпад **удалён** из проекта.
- SDL GameController, маппинг добавляется в `add_dev_mapping()` в `host/gof2hd.c`:
  `"a:b0,b:b3,x:b2,y:b1,leftshoulder:b7,rightshoulder:b6,lefttrigger:b5,righttrigger:b4,
    back:b12,start:b9,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,
    leftx:a0,lefty:a1,rightx:a2,righty:a3"`
- Hat-значения крестовины (прошивка): UP=1, RIGHT=2, DOWN=4, LEFT=8
  (сдвиг от SDL-стандарта 0/1/2/3 — уже учтено в маппинге).
- Логика: левый стик → курсор (скорость ∝ отклонению, deadzone 4000,
  константы в gof..: `SDL_CONTROLLER_AXIS_LEFTX/LEFTY`), крестовина → шаг курсора
  10 px, A (`b0`) → touch down/up (pid 722), B (`b3`) → BackButtonPressed.
- Лог: `[pad] mapped/opened ANBERNIC-keys (idx 0)`, `[pad] gamepad ready`.

### Сборка и запуск — 3 команды
```sh
# 1) скопировать порт на устройство (с ПК)
scp -r port/ root@192.168.0.128:/root/gof2hd/
# 2) собрать прямо на устройстве (armhf, без кросскомпилятора)
cd /root/gof2hd/port && bash tools/build-native.sh   # ждать "== done =="
# 3) запуск одной командой (сам гасит лаунчер, убивает старую игру, поднимает лог)
bash /root/gof2hd/port/tools/start-game.sh
# лог: tail -f /root/gof2hd/run.txt
```
- Сборка идёт ТОЛЬКО на устройстве (`gcc/g++ arm-linux-gnueabihf`, SDL2 dev
  в системе). `build-native.sh` собирает: shim (libc/liblog/libandroid/libm/libdl),
  GLES+EGL мост (`gles-bridge.c` → `libGLESv2.so`, из него же `libEGL.so(.1)`),
  FMOD-заглушки, хост `gof2hd` (`-lSDL2`), патчит e_flags игры soft→hardfp.
- `start-game.sh` ставит env: `SDL_VIDEODRIVER=mali`, `SDL_AUDIODRIVER=dummy`,
  `GOF_SHOW_CURSOR=1`, `LD_LIBRARY_PATH=.:/usr/lib32`.

### Что удалено из репозитория (почищено 2026-08-07)
- `port/tools/build.sh` (старая softfp-сборка), `build-hardfp.sh` (старая кросс-сборка).
- `port/Makefile`, `gles-stub/gles-stub.c` (софт-GLES), `viewer/viewer.c`,
  `port/shim/gen_shim.py`. `port/viewer/` — пусто, удалено. `.gitignore` почищен.
- `port/pad/pad-server.c` + `pad-client.py` — виртуальный геймпад (uinput) удалён.
- Из `gof2hd.c` убран костыль `legacy_sscanf` (сборка на устройстве, glibc 2.35 —
  обычного `sscanf` достаточно; в коде снова нормальный `sscanf("%d %d %d %d", ...)`).
- Из `gles-bridge.c` убраны ВСЕ GLES1-функции — движок импортирует только GLES2
  (проверено `readelf`; GLES1-функций в UND-списке движка нет).

### Важные операционные нюансы
- **Не использовать `pkill -f`/`pgrep -f` с совпадением имени** — фантом убивает
  саму команду. Процессы убивать только по PID, причём `kill -9` (SIGTERM ловится
  crash-handler'ом игры, не срабатывает).
- SSH из этой среды: `export SSH_ASKPASS=/tmp/opencode/askpass.sh
  SSH_ASKPASS_REQUIRE=force DISPLAY=dummy:0`.
- git push невозможен из среды (SSH с ключом отклоняется) — пушит пользователь.

### Устаревшие/исторические разделы ниже
Разделы 3–12 описывают путь: софт-GLES стаб, CPU-цикл, противоречие
softfp/hardfp и т.д. Они оставлены как история решений — это НЕ гайд по сборке.

---

## 1. Цель

Запустить нативный Android-бинарь игры `libgof2hdaa.so` на Linux-устройствах с
ARM-процессором, используя аппаратный GPU (Mali) и нативную скорость (без qemu).

## 2. Состав проекта (`port/`)

| Компонент | Файл | Роль |
|---|---|---|
| **host** | `host/gof2hd.c` | Заменяет Java-обёртку Android: вызывает JNI-функции движка |
| **JNI-эмуляция** | `host/jni.c`, `jni.h` | Фейковые JNIEnv/JavaVM/jstring |
| **libc shim** | `shim/shim.c`, `abi.c`, `sscanf.c` | Экспортирует bionic-символы `@LIBC`, форвардит в glibc |
| **libm shim** | `shim/libm.c`, `libm.map` | Math-символы `@LIBC` (acosf/sinf/sqrtf...) |
| **GLES bridge** | `gles-stub/gles-bridge.c` | Мост softfp→hardfp для libmali |
| **fmod stubs** | `fmodex-stub/` | Заглушки аудио FMOD |
| **Сборка** | `tools/build-native.sh` | Нативная сборка на устройстве |

## 3. Как игра работает (механика запуска)

Android-приложение состоит из Java-обёртки + нативного `.so`:
- Java (`GOF2HD2012.java`, `ToJNI.java`) вызывает нативные JNI-функции:
  `setZIPPath`, `SetDirectories`, `setAPKPath`, `setEnvironmentVariables`,
  `setCountryCodeOfDevice`, `SetOrigamiSuperClub`, `initialize`, `resize`,
  `renderstep`, `getExitFlag`, `getLogoShown`, `isInMainMenu`, `handleTouchEvent`.
- Наш `host/gof2hd.c` повторяет эту последовательность через `dlsym`.
- Игра загружается через `dlopen("libgof2hdaa.so")`.

**Последовательность запуска (работает):**
1. `JNI_OnLoad`
2. `setZIPPath(obb)` — путь к OBB-архиву с ресурсами
3. `SetDirectories(dataDir, obbdir)`
4. `setAPKPath(base.apk)`
5. `setEnvironmentVariables(context)`
6. `setCountryCodeOfDevice(0)`
7. `SetOrigamiSuperClub("...")` — иначе краш в SHA256 (NULL-указатель)
8. `initialize(640,480)` → `resize(640,480)`
9. Цикл: `renderstep(now_ms())` каждые ~33мс

**Проверенные факты:**
- Игра **запускается и доходит до рендер-цикла** (logo показывается, кадры идут).
- Touch-событие формата `(pointerId, action, x, y)` (pointerId=722) сбрасывает
  логотип (logo: 1→0). Формат из `TouchHandler.java`.
- Игра читает контент **напрямую из OBB через zip** (libzip), НЕ распаковывает
  на диск. `dataDir` остаётся пустым.
- После логотипа уходит в **бесконечный CPU-цикл** (см. раздел 6).

## 4. КРИТИЧНО: ABI игры — softfp

**Подтверждено дизассемблером `libgof2hdaa.so`:**
- `sqrtf` вызывается так: `vmov r0, s2` (float кладётся в **r0**), `blx sqrtf`,
  `vmov s0, r0` — это **softfp** (аргументы в r-регистрах/стеке).
- `glClearColor` вызывается с `movs r0-r3, #0` — целочисленные регистры (softfp).
- `glUniform4f` — float через `vstr s0, [sp]` (на стек, softfp).
- `Tag_ABI_HardFP_use: Deprecated`, **нет** `Tag_ABI_VFP_args` = softfp ABI.
  (VFP используется для вычислений, но НЕ для передачи аргументов.)

**Вывод:** игра — bionic armeabi-v7a **softfp**. Это критично: определяет
совместимость со всем стеком.

## 5. ABI устройств

### muOS (RG40XX-H, портативная консоль)
- Ядро aarch64, `CONFIG_COMPAT=y` (32-бит работает).
- glibc: **только armhf (hardfp)** в `/lib32`.
- GPU: `/dev/mali0`, `/usr/lib32/libmali.so` — **hardfp**.
- SDL2 в `/usr/lib32` — hardfp.
- **Никакого armel/softfp окружения нет.**

### Стоковая прошивка Anbernic (Ubuntu 22.04, aarch64)
- `CONFIG_COMPAT=y`.
- gcc 11.4 нативно + `gcc-arm-linux-gnueabihf` (кросс → armhf, hardfp).
- glibc armhf hardfp в `/usr/lib/arm-linux-gnueabihf/`.
- `/dev/mali0`, `/usr/lib32/libmali.so` — hardfp.
- **Никакого softfp/armel окружения нет.**

### x86_64 хост (для qemu)
- qemu-user + armv5 softfp glibc (Bootlin) — softfp-стек привозится с собой.

## 6. Основной баг после запуска: бесконечный CPU-цикл

**Симптом:** игра проходит ~240 кадров (логотип), затем уходит в бесконечный
цикл без I/O (rchar не растёт, CPU 100%). Точка: софт-GLES `emit_tri` →
`__aeabi_fmul` (флоат-умножение в растеризаторе).

**Диагностика через gdbserver (работает):** PC в `__aeabi_fmul`, LR в
`emit_tri` — бесконечные вызовы растеризации. Причина: либо вырожденные
треугольники, либо неправильные атрибуты/матрицы (наш софт-растеризатор
игнорирует шейдеры).

**Важно:** этот баг был в **софт-GLES стабе**. С аппаратным GLES его не будет.

## 7. ABI-мост softfp → hardfp (проверен, работает)

Мы нашли способ совместить softfp-игру с hardfp-библиотекой:

```c
typedef void (*clear_hf)(float,float,float,float) __attribute__((pcs("aapcs-vfp")));
__attribute__((pcs("aapcs"))) void clear_soft(float r,float g,float b,float a) {
    clear_hf(r,g,b,a);  // hardfp-вызов внутрь
}
```

- `pcs("aapcs")` = softfp-приёмник (float приходит в r0-r3)
- `pcs("aapcs-vfp")` = hardfp-указатель (float в s0-s15)

**Доказано тестами на устройстве:** hardfp-процесс, softfp-функция вызывает
hardfp libmali — работает. Variadic (`printf` с float) через pcs aapcs — работает.

**`gles-bridge.c`** содержит все 68 GLES2-функций с этой схемой. Собран hardfp,
dlopen'ит libmali. Проверен: `bridge call ok`.

## 8. Проблема версий символов (решена для pthread)

Когда shim включает `<pthread.h>`, glibc навязывает нашим определениям
pthread_* версию `GLIBC_2.4`, блокируя `@@LIBC`-экспорт.

**Решение:** НЕ включать `<pthread.h>`/`<semaphore.h>`. Все pthread-типы уже
доступны из `<sys/types.h>` (→ `bits/pthreadtypes.h`). sem_t определён вручную
(`shim/types.h`, 16 байт).

**Результат:** после этого `pthread_mutex_lock@@LIBC`, `pthread_cond_wait@@LIBC`,
`__stack_chk_fail@@LIBC`, `__stack_chk_guard@@LIBC` — все экспортируются.
Все 79 символов `@LIBC`, требуемых игрой, покрыты shim'ом.

## 9. ГЛАВНОЕ ПРОТИВОРЕЧИЕ (нерешено)

```
Игра (softfp)  ──(hardfp loader)──>  НЕ ЗАГРУЖАЕТСЯ
```

**Доказано:** hardfp loader `/lib/ld-linux-armhf.so.3` физически не может
загрузить softfp `.so` (возвращает "cannot open shared object file" / "uses VFP
register arguments"). А на стоковой прошивке ВЕСЬ системный стек — hardfp:
glibc, libmali, SDL2.

Итого два взаимоисключающих требования:
1. Игра softfp → нужен softfp-процесс.
2. libmali (аппаратный GPU) hardfp → нужен hardfp-процесс.

**Одновременно на одном устройстве с одной glibc это невозможно.**

### Что проверено и не работает
- hardfp-процесс загружает softfp-игру → отказ (этот баг).
- softfp-процесс загружает hardfp-libmali → "internal error" (две glibc в
  одном процессе конфликтуют).

### Что работало на muOS
На muOS использовался **привезённый softfp glibc stack** (armv5-eabi из
Bootlin toolchain) + **софт-GLES стаб**. Игра загружалась, логотип работал.
Аппаратный Mali не использовался.

## 10. Возможные пути решения (для обсуждения с LLM)

### A. Привезти softfp glibc с собой + софт-GLES
- Собрать весь стек softfp (host, shim, софт-растеризатор) с armv5-eabi glibc.
- Игра работает, но **без аппаратного ускорения**.
- На RG40XX-H с CPU H700 софт-рендер может быть достаточно для меню.

### B. Конвертация игры в hardfp
- Пересобрать/пропатчить ARM-код `libgof2hdaa.so` softfp→hardfp.
- Рискованно: потребует ABI-трансформации всех вызовов с float.
- Теоретически возможно через обёртки, но практически очень сложно.

### C. Использовать второй процесс для GLES (offload)
- Игра (softfp) рендерит в память/файл, hardfp-процесс показывает через Mali.
- Требует передачи кадров (shared memory / socket). Сложно, но аппаратное.

### D. Эмуляция Android-слоя поверх
- Запустить bionic libc (собрать из AOSP) для softfp-процесса.
- GPU через gl4es/GL-on-GLES мост. Очень сложно.

### E. Другое устройство/прошивка с softfp-стеком
- Если есть устройство с armel/softfp glibc + Mali — всё бы заработало.

## 11. Технические заметки для продолжения

### Запуск на устройстве
```sh
# на устройстве
cd /root/gof2hd/port
./tools/build-native.sh   # собирает run-native/
cd run-native
export LD_LIBRARY_PATH=/root/gof2hd/port/run-native:/usr/lib32
./gof2hd <base.apk> <main.*.obb> <dataDir> 640 480
```

### gdbserver (кросс-отладка зависаний)
```sh
# на устройстве
gdbserver 0.0.0.0:2345 ./gof2hd ...
# на хосте
arm-buildroot-linux-gnueabi-gdb ./host/gof2hd
# внутри gdb:
target remote <dev-ip>:2345
```

### Ключевые файлы для ABI-моста
- `gles-stub/gles-bridge.c` — генерация: функция `pcs("aapcs")` внутри
  вызывает `pcs("aapcs-vfp")` указатель на libmali.
- `shim/shim.c` — float/variadic функции помечены `SF` (pcs aapcs).
- `shim/types.h` — sem_t (16B), pthread-типы берутся из sys/types.h.

### Проверенные размеры типов (armhf glibc)
```
pthread_mutex_t = 24 B
pthread_cond_t  = 48 B
pthread_attr_t  = 36 B
pthread_mutexattr_t = 4 B
pthread_condattr_t  = 4 B
sem_t           = 16 B
pthread_key_t   = 4 B (unsigned int)
pthread_once_t  = 4 B
```

### Энва-флаги для отладки
- `GOF_GL_TRACE=1` — трассировка GL-вызовов
- `GOF_FB=/dev/fb0` — софт-GLES пишет кадры прямо в framebuffer
- `GOF_AUTO_TOUCH=1` — авто-тап в центр (сброс логотипа)
- `GOF_GDB=1` — не ставить crash-handler (для gdb)
- `GOF_VERBOSE_JNI=1` — вывод JNI-вызовов

### Формат touch (из TouchHandler.java)
```
handleTouchEvent(pointerId, action, x, y)
pointerId = 722 + индекс пальца
action: 0=down, 1=up, 2=move
```

## 12. История ключевых решений

1. **fseeko/ftello ABI** — игра передаёт 32-bit off_t (r1=offset, r2=whence).
   Исправлено: сигнатуры `long`, форвард на fseeko64/ftello64. → zip работает.
2. **SetOrigamiSuperClub** — обязателен, иначе NULL-крах в SHA256_Update.
3. **Формат touch** — был (x,y,0,0), стало (722,action,x,y). → логотип сбрасывается.
4. **Софт-GLES растеризатор** — NaN-защита добавлена, но игра всё равно
   зацикливается (недостаточный рендер без шейдеров).
5. **ABI-мост pcs** — подтверждён рабочим на устройстве.
6. **pthread версии** — убраны <pthread.h>/<semaphore.h>, типы из sys/types.h.
   Все 79 @LIBC символов теперь экспортируются.
7. **Главный блокер** — hardfp loader не грузит softfp-игру. Стоковая
   прошивка несовместима с игрой на уровне ABI без дополнительного слоя.

---

## Сводка для внешней LLM

Мы портируем Android-игру (ARMv7, **softfp** ABI, bionic libc) на ARM Linux.
Движок загружается, логотип работает. Проблема: **hardfp Linux (glibc + Mali)
не может загрузить softfp-бинарник**, а игра — однозначно softfp. Ищем способ
запустить softfp-игру с аппаратным Mali (или иной путь к запуску). Ключевые
подсказки: ABI-мост через `pcs("aapcs")`/`pcs("aapcs-vfp")` работает на уровне
вызовов, но loader отвергает softfp .so на hardfp glibc. Нужно решение для
связки "softfp код + hardfp системные библиотеки" или альтернативный путь.
