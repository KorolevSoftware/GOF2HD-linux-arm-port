# GOF2HD Linux Port — технический отчёт

Дата: 2026-08-07
Проект: порт Galaxy on Fire 2 HD (Android, ARMv7, bionic) на Linux ARM-устройства.
Статус: **работает** — игра запускается и рендерится на аппаратном GPU (Mali)
через SDL2-окно + ABI-мост softfp→hardfp.

## 1. Устройство (эталон)

- **ANBERNIC (стоковая прошивка), root@192.168.0.128, armhf glibc 2.35 + aarch64 kernel.**
- Пути: `/root/gof2hd/` (проект), `/root/gof2hd/port/run-native/` (собранное),
  `/root/gof2hd/port_bak/` (бэкап), `/dev/fb0` (framebuffer), `/dev/input/js0` (геймпад).
- GPU: настоящие EGL/GLES — в `/usr/lib32/libmali.so.0.20.0`. Пустышка
  `/usr/lib32/libEGL.so.1.4.0` (2968 Б, без EGL-символов).
- glibc 2.35: `sscanf` резолвится как `__isoc99_sscanf@GLIBC_2.7` — обычный
  `sscanf` использовать можно.
- Файлы игры: `base.apk`, `main.<ver>.net.fishlabs.gof2hdallandroid2012.obb`,
  `libgof2hdaa.so` (движок, извлечён из APK `lib/armeabi-v7a/`).

## 2. Видео-путь (работает)

- Хост рисует через **SDL2** (видеодрайвер `mali`, ядро fb0) + EGL.
- SDL сам находит и грузит EGL (несмотря на пустышку `/usr/lib32/libEGL.so.1`
  без символов — SDL2 пробует свои пути, включая libmali), поэтому никакого
  EGL-кода или подложенных библиотек в порте нет вовсе.
- В логе: `SDL window ready (video driver: mali)`, `logo=1` — рендер идёт.

## 3. Геймпад (ввод)

- **Встроенный** геймпад консоли `ANBERNIC-keys` (js0/event1, gpio-keys-polled).
  Виртуальный uinput-геймпад **удалён** из проекта.
- SDL GameController, маппинг добавляется в `add_dev_mapping()` в `host/gof2hd.c`:
  `"a:b0,b:b3,x:b2,y:b1,leftshoulder:b7,rightshoulder:b6,lefttrigger:b5,righttrigger:b4,
    back:b12,start:b9,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,
    leftx:a0,lefty:a1,rightx:a2,righty:a3"`
- Hat-значения крестовины (прошивка): UP=1, RIGHT=2, DOWN=4, LEFT=8
  (сдвиг от SDL-стандарта 0/1/2/3 — уже учтено в маппинге).
- Логика: стик и крестовина складываются в единый нормализованный вектор
  в `pump_input_vector()` (`gof2hd.c` — бэкенд: deadzone 4000, ±1 от
  крестовины; крестовина дублирует стик — консоли без аналоговых стиков),
  вектор кормит `WrawState` в `wrap_overlay.c`. В режиме курсора вектор
  двигает курсор (скорость ∝ отклонению, `overlay_input_vector`),
  A (`b0`) → touch down/up (pid 722), B (`b3`) → BackButtonPressed, X —
  fire (pid 723).
- **Гироскоп-режим** (`WrawState.mode`, тумблер START или env `GOF_GYRO=1`):
  вектор стика+крестовины эмулирует акселерометр
  → `Java_..._ToJNI_handleAccelerometer`. В режиме гироскопа курсор
  не двигается (вектор весь уходит акселерометру), геттер
  `overlay_get_gyro()` отдаёт итоговый маппинг.
- Лог: `[pad] mapped/opened ANBERNIC-keys (idx 0)`, `[pad] gamepad ready`,
  `[pad] gyro mode ON/OFF`.

### Гироскоп-стик: как это работает (реверс)

Движок управляет кораблём от акселерометра: `MGame::handleAccelerometer()`
читает `Engine::GetAccelValue()` (3 double: X/Y/Z из `SetAccelValue`):
- **руль** (left/right) — по `accel[1]` (Y), масштаб ×2.5, порог ±1.0 (т.е. ±0.4);
- **питч** (up/down) — по `accel[2]` (Z): формула `p = f18 - (2-X)` при Z>0,
  сдвиг на константу; нейтраль = **Z ≈ +1.0** (в оригинале Android шлёт
  `z/10 ≈ 0.98` при ровном устройстве).

**ABI-ловушка (критично):** движок softfp (float в r0–r3), host hardfp
(aapcs-vfp, float в s0–s2). Указатель на JNI-функцию с float-аргументами
обязательно помечать `__attribute__((pcs("aapcs")))` — иначе движок читает
мусор из r0–r3 и корабль не реагирует вообще. JNI-стаб `handleAccelerometer`
дополнительно инвертирует первый float: движок видит `SetAccelValue(-a, b, c)`.

**Итоговый маппинг** в `overlay_get_gyro()` (`wrap_overlay.c`), вызывается
каждый кадр из `pump_engine_input()`:
```
ax = 0.0f;      // engine X не используется для руля/питча
ay = -nx;       // engine Y (руль) = -стик X  (стик вправо -> корабль вправо)
az = 1.0f + ny; // engine Z (питч) = 1.0 + стик Y  (нейтраль Z=1, вверх -> >1)
```
Проверено на устройстве: руль и питч работают, в нейтрали корабль летит ровно.

### Курсор-оверлей (`wrap_overlay`) и движение стиком

**Почему курсор мигал (история).** Старый вариант рисовал крест-прицел
напрямую в память `/dev/fb0` (mmap, BGRA) сразу **после** `SDL_GL_SwapWindow`.
Презентация кадра драйвером mali-fbdev (свап/пан, `SDL_GL_SetSwapInterval(0)` —
неблокирующий) асинхронно перезаписывала этот регион каждый кадр => курсор
был виден только в короткие окна и моргал на кадровой частоте.

**Решение.** Модуль `port/host/wrap_overlay.c` рисует крест в **тот же GL-кадр**:
вызов после `p_renderstep()`, но **до** `SDL_GL_SwapWindow()` (см. `sdl_swap()`
в `gof2hd.c`). Курсор презентуется вместе с кадром и физически не может
моргать. Шейдер минимальный: `attribute vec2 aPos` + `uniform mat4 uProj`
(ортогональная 2D-проекция, строится один раз из разрешения игры
в `overlay_init(w,h)`) + `uniform vec4 uColor`, без текстур и блендинга.
Перед отрисовкой состояние движка сохраняется (`glGetIntegerv`:
CURRENT_PROGRAM / ARRAY_BUFFER_BINDING / VIEWPORT), после — восстанавливается.

**ABI-последствия линковки.** `gof2hd` линкуется с мостом `libGLESv2.so`
напрямую (полный путь в build-native.sh), без `dlopen`/`dlsym`. Но мост
экспортирует softfp-входы (`pcs("aapcs")`), а хост hardfp => каждый прототип
в `wrap_overlay.c` помечается `__attribute__((pcs("aapcs")))` — float-аргументы
(`glUniform4f`, `glUniformMatrix4fv`) едут в r0–r3. Цена: потеря символа —
ошибка линковки, а не тихий фейл.

**Грабли линковки.** Если добавить `-L"$OUT"` (run-native) в строку хоста —
`-ldl` первым найдёт пустой стаб `run-native/libdl.so` (пустышка для движка)
и упадёт `undefined reference to dlerror@@GLIBC_2.34`. Линковать именно
абсолютным путём `"$OUT/libGLESv2.so"` (DT_NEEDED с полным путём).

**Движение курсора (polling).** `SDL_CONTROLLERAXISMOTION` приходит
только пока значение оси меняется: удерживаемый в отклонении стик —
и курсор стоял. Исправлено: `gof2hd.c` опрашивает левый стик каждый
кадр и складывает с флагами крестовины в единый вектор (`pump_input_vector`),
который передаёт в `overlay_input_vector()` (`wrap_overlay.c`); там —
скорость ∝ отклонению (deadzone 4000, макс 8 px/кадр ≈ 240 px/с),
дробный аккумулятор для плавного медленного хода (`WrawState.rem`).
В gyro-режиме вектор уходит акселерометру, курсор не двигается.

## 4. Сборка и запуск — 3 команды

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
  GLES+EGL мост (`gles-bridge.c` → `libGLESv2.so`; EGL — SDL находит сам),
  FMOD-заглушки, хост `gof2hd` (`-lSDL2`; `wrap_overlay.c` линкуется
  напрямую с `run-native/libGLESv2.so` по полному пути — НЕ через `-L`,
  чтобы каталог run-native не попал в поиск `-l` и `-ldl` не схватил
  пустой стаб `run-native/libdl.so`), патчит e_flags игры soft→hardfp.
- `start-game.sh` ставит env: `SDL_AUDIODRIVER=dummy` (видеодрайвер SDL2
  выбирает сам),
  `GOF_SHOW_CURSOR=1`, `LD_LIBRARY_PATH=.:/usr/lib32`.

Ручной запуск:

```sh
cd /root/gof2hd/port/run-native
export SDL_AUDIODRIVER=dummy GOF_SHOW_CURSOR=1
export LD_LIBRARY_PATH=/root/gof2hd/port/run-native:/usr/lib32
./gof2hd /root/gof2hd/base.apk \
    '/root/gof2hd/obb/net.fishlabs.gof2hdallandroid2012/main.47947006.net.fishlabs.gof2hdallandroid2012.obb' \
    /root/gof2hd/data
```

## 5. Как игра работает (механика запуска)

Android-приложение = Java-обёртка + нативный `libgof2hdaa.so`. Наш
`host/gof2hd.c` повторяет последовательность Java через `dlsym`/`dlopen`.

**Последовательность запуска (работает):**
1. `JNI_OnLoad`
2. `setZIPPath(obb)` — путь к OBB-архиву с ресурсами
3. `SetDirectories(dataDir, obbdir)`
4. `setAPKPath(base.apk)`
5. `setEnvironmentVariables(context)`
6. `setCountryCodeOfDevice(0)`
7. `SetOrigamiSuperClub("...")` — иначе краш в SHA256 (NULL-указатель)
8. `initialize(w,h)` → `resize(w,h)` — w/h = разрешение экрана,
   полученное от SDL (полный экран, `SDL_GetWindowSize`)
9. Цикл: `renderstep(now_ms())` каждые ~33 мс

**Проверенные факты:**
- Игра **запускается и рендерит** (логотип, меню, кадры идут).
- Touch-событие `(pointerId, action, x, y)` (pointerId=722, 0=down/1=up/2=move)
  из `TouchHandler.java` — A-кнопка шлёт down+up.
- Контент читается **напрямую из OBB через zip** (libzip), НЕ распаковывается;
  `dataDir` остаётся пустым.

## 6. КРИТИЧНО: ABI игры — softfp (и решение)

**Подтверждено дизассемблером `libgof2hdaa.so`:**
- float-аргументы передаются в r0–r3 / на стек (`vmov r0, s2`; `vstr s0, [sp]`);
  `Tag_ABI_HardFP_use: Deprecated`, нет `Tag_ABI_VFP_args` → **softfp ABI**.

На устройстве весь системный стек — hardfp (glibc, libmali, SDL2), а hardfp
loader не грузит softfp `.so`. Это главное противоречие проекта.

**Решение — ВЫБРАНО И РАБОТАЕТ:** конвертировать вызовы игры softfp→hardfp
через ABI-мост учётом бинарника. Суть моста (см. `gles-bridge.c`):

```c
typedef void (*clear_hf)(float,float,float,float) __attribute__((pcs("aapcs-vfp")));
__attribute__((pcs("aapcs"))) void clear_soft(float r,float g,float b,float a) {
    clear_hf(r,g,b,a);  // hardfp-вызов внутрь libmali
}
```

- `pcs("aapcs")` = softfp-приёмник (float приходит в r0-r3)
- `pcs("aapcs-vfp")` = hardfp-указатель (float в s0-s15)

Обёртки живут в: `gles-stub/gles-bridge.c` (GLES→libmali), `shim/libm.c`
(float-math), `shim/stdio.c`, `shim/abi.c`. EGL движок не использует:
контекст создаёт SDL, а тот находит EGL сам (см. §2) — никакого кода EGL
в мосте нет.

**Почему нельзя просто `SF float acosf(float a) { return acosf(a); }`:**
- Это рекурсивный вызов самого себя (компилятор видит вызов символа `acosf`,
  который мы и определяем) → бесконечная рекурсия / переполнение стека.
- Даже если бы имя резолвилось на glibc-функцию, вызов без `pcs("aapcs-vfp")`
  передал бы float неверно: движок кладёт float в r0-r3 (softfp), а hardfp
  функция ждёт его в s0-s15. `pcs` входит в тип, поэтому нужен явный указатель
  с правильной конвенцией.

Правильный паттерн (`shim/libm.c`):
```c
typedef float (*HF)(float) __attribute__((pcs("aapcs-vfp")));
static HF gl_acosf;                       // тип = hardfp (float в s0-s15)
__attribute__((constructor)) void libm_init(void) {
    gl_acosf = (HF)dlsym(dlopen("libm.so.6", RTLD_NOW), "acosf"); // настоящий символ
}
SF float acosf(float a) { return gl_acosf(a); }  // вызов через HF-pointer
```

Так работает и в `gles-bridge.c` (каждая GLES2-функция: `__SF` обёртка +
`__HF` указатель на libmali), и в `stdio.c`/`shim.c`.

**Важно о GLES:** движок импортирует **только GLES2-символы** (проверено
`readelf`; GLES1-функций в UND-списке нет), поэтому в мосте только они.
EGL в движке нет вовсе — его контекст не создаётся игрой.

## 7. Версии символов и маппинг на glibc (решено: ELF-патч)

Движок импортирует bionic-символы с версией `@LIBC`; glibc не имеет такого
узла. **Решение — гибридный ELF-патч** (`tools/patch-versions.py`).

### Как функции попадают в glibc или в shim

После патча каждый импорт движка резолвится ровно одним из двух путей:

1. **`@LIBC` остаётся** у символов, которые предоставляет наш shim:
   stat/fstat/mktime, FILE-пул (`__sF`, fopen/fread/...), pthread_*,
   softfp-математика (`sinf` и т.п. через `libm.so`). Динамический линкер
   обязан резолвить их в shim (единственный провайдер узла `LIBC`) — именно
   это гарантирует попадание в наши ABI-трансляторы (см. §6).
2. **Версия зануляется** у всех прочих импортов (malloc/strlen/memcpy/open/...):
   они становятся unversioned и биндятся **сразу в glibc** (первый провайдер
   в scope) — без обёрток-форвардеров. Т.е. обычные функции libc «мапаются
   на glibc» автоматически самим динамическим линкером: патчер лишь убирает
   версию `@LIBC`, чтобы glibc (а не наш shim) их подхватил.

### Роль `tools/patch-versions.py`

- Читает `.dynsym`/`.dynstr`/`.gnu.version` движка и экспорт собранных
  `libc.so`/`libm.so`; «keep-list» (что оставить `@LIBC`) берётся **из
  реального экспорта shim-библиотек**, поэтому ручная поддержка списка не
  нужна — добавил символ в shim → патчер автоматически оставит ему версию.
- `.gnu.version_r`/DT_VERNEED **не трогаются** — узел `LIBC` остаётся
  резолвимым для shim-набора.
- Вызывается из `tools/build-native.sh` после e_flags-патча:
  `python3 tools/patch-versions.py <engine.so> <shim/libc.so> <shim/libm.so>`.

**Важно (урок):** нельзя занулять ВСЕ версии сразу — unversioned-импорты
резолвятся по порядку scope, где glibc libc.so.6 (зависимость host-бинаря)
стоит раньше нашего `./libc.so`, поэтому трансляционные обёртки
(stat/FILE/pthread) молча перестают использоваться и игра падает в glibc на
bionic-структурах (крэш `fault 0x8` внутри pthread/stdio).

**Типы структур на устройстве (armhf glibc):** pthread_mutex_t=24, cond=48,
attr=36, mutexattr=4, condattr=4, sem_t=16, key_t=4, once_t=4.

## 8. Нюансы проекта (уже учтено в коде)

- Сборка только на устройстве: старые кроссприйл-скрипты, софт-GLES стаб,
  viewer и виртуальный uinput-геймпад **удалены** из репозитория.
- В `gof2hd.c` убран костыль `legacy_sscanf` (glibc 2.35 — обычный `sscanf`
  допустим; в коде снова `sscanf("%d %d %d %d", ...)`).

## 9. Операционные нюансы

- **Не использовать `pkill -f`/`pgrep -f` с совпадением имени** — фантом
  убивает саму команду. Процессы убивать только по PID, `kill -9` (SIGTERM
  ловится crash-handler'ом игры).
- SSH из этой среды: `export SSH_ASKPASS=/tmp/opencode/askpass.sh
  SSH_ASKPASS_REQUIRE=force DISPLAY=dummy:0`.
- git push невозможен из среды (SSH с ключом отклоняется) — пушит пользователь.
- gdbserver (armhf): ставится apt'ом прямо на устройстве —
  `apt-get install -y gdbserver` (Ubuntu 22.04 armhf). Запуск с нужным env:
  ```sh
  cd /root/gof2hd/port/run-native
  setsid bash -c 'export GOF_FB=/dev/fb0 GOF_SHOW_CURSOR=1 SDL_AUDIODRIVER=dummy \
    LD_LIBRARY_PATH=.:/usr/lib32; \
    exec gdbserver 127.0.0.1:2345 ./gof2hd base.apk main.*.obb data' \
    >/root/gof2hd/gdblog.txt 2>&1 </dev/null &
  ```
  Туннель с ПК: `ssh -N -L 2345:127.0.0.1:2345 root@192.168.0.128`.
  Клиент (ПК, gdb-multiarch — уже стоял на Manjaro):
  `gdb-multiarch -batch -ex 'set solib-search-path <копия run-native>' -ex
  'target remote 127.0.0.1:2345' -ex 'continue' -ex 'bt' ./gof2hd`.
  **Важно:** под gdbserver движок на старте ловит SIGILL в ld.so и
  зависает — интерактивная трассировка основной игры пока не работает;
  для постмотра crash-переписей используем краш-хендлер + /proc/self/maps.
  core_pattern на устройстве перезаписать нельзя (readonly) — путь к
  coredump «core» идёт в CWD процесса.
- Энва-флаги отладки: `GOF_TRACE` (GL-трейс),

  `GOF_GDB`, `GOF_VERBOSE_JNI`, `GOF_SHOW_CURSOR` (курсор-прицел,
  рисуется модулем `wrap_overlay` в GL-кадре перед свапом).
## 10. Структура проекта

| Компонент | Файл | Роль |
|---|---|---|
| host | `host/gof2hd.c` | SDL2-окно/EGL + геймпад-ввод (бэкенд: deadzone, стик+крестовина → вектор) + движение Java-обёртки |
| GL-оверлей + состояние ввода | `port/host/wrap_overlay.c/.h` | `WrawState` (режим курсор/гиро, курсор, кнопки, вектор ввода) + курсор-прицел в GL-кадре перед свапом; линкуется с `libGLESv2.so` (`pcs("aapcs")`-прототипы) |
| JNI-эмуляция | `host/jni.c`, `jni.h` | Фейковые JavaVM/Jobject/jstring |
| libc shim | `shim/shim.c`, `abi.c`, `sscanf.c`, `stdio.c` | трансляция `@LIBC`-набора (FILE/stat/pthread), спецсимволы |
| libm shim | `shim/libm.c`, `libm.map` | float-math `@LIBC` (softfp→hardfp) |
| GLES bridge | `gles-stub/gles-bridge.c` | softfp→hardfp GLES2 в libmali (EGL движок не использует) |
| fmod stubs | `fmodex-stub/` | Заглушки аудио FMOD — ВСЕ геттеры возвращают NULL (fake-объекты запрещены, см. §11) |
| Патч версий | `tools/patch-versions.py` | зануляет VERSYM у не-shim импортов движка |
| Сборка | `tools/build-native.sh` | всё на устройстве |
| Запуск | `tools/start-game.sh` | одна команда |

## 11. FMOD-аудио: хроника крашей и планируемый фикс диалогов (2026-08-08)

### 11.1. Памятка по структуре FModSound (из Ghidra/дизассемблера)

`FModSound` (vtable сброшен):
```
this+0x18   + id*4   — слоты событий: sids 0..0x8F4 (Event*)
this+0x23F0 ..+0x23F8  — слоты «SFX» (3 шт, индексы 0..2, стоп-цикл ходит по 1..2)
this+0x23FC           — указатель EventSystem (нулевой = звук отключён)
this+0x2404           — bool «звук включён» (guard у play/isPlaying/progress)
this+0x2410 ..+0x2424 — список «играющей музыки» (5 int, -1=пусто)
this+0x2434/0x2438    — копии позиций (Vector*) для 3D-событий
this+0x11             — bool «играет музыка вообще» (собирается из слотов)
```
Ключевые методы (смещения в `libgof2hdaa.so`, Thumb):
- `play(sid, ...)` @0x92F3FC — при 0x2404!=0 и sid<0x8F5 кладёт `Event*` в
  слот `this+0x18+sid*4` (из `getEventBySystemID`) и зовёт setPitch/getCategory;
- `isPlaying(sid)` @0x931E4 — слот пуст → false, иначе `Event::getState` → bit3;
- `isChannelActive(sid)` @0x93228 — getState → bit4;
- `stopAllSoundFXEvents()` @0x92DAC — цикл по слотам 1,2 (0x23F0), зовёт
  `(**slot+0x1C)()` (= vtable[7], вирт. `stop()`);
- `getPlayingProgress(sid)` @0x93988 / `getEventPauseLength` @0x93A14 — через
  `Event::getInfo`/`getProperty` (у нас no-op).

### 11.2. Краш №1 — детерминированный (был до правок)

- Симптом: SIGSEGV на заходе на станцию, `pc=0xf6184dc6` (база 0xf60f2000 →
  offset **0x92DC6**), `fault (nil)`: `ldr r1,[r0]` при r0=NULL — слот 0x23F0
  (1..2) содержал NULL.
- Цепочка: стабы `getEventBySystemID` возвращали `*e=NULL, FMOD_OK` → `play()`
  **без проверки кладёт NULL в слот** → при первой же остановке SFX
  (заход на станцию, грузится 3× `stations.bin`) цикл разыменовывает NULL.
- Подтверждено Ghidra-декомпиляцией (обоих путей) до мелочей.

### 11.3. Неудачный фикс: фейковые объекты (порождал новый краш)

Попытка: стабы вернули «валидные» fake-объекты с vtable из 16 no-op —
краш №1 исчез, но появился **краш №2**: `pc`/`lr` оказались ВНУТРИ таблицы
строк движка (offset 0x193D7/0x193EA, участок `.rodata` со строками
`_ZN20FileInterfaceAndroid15...`), `r4=0x8f` (sid 143, звуки станции),
fault-адрес = r0+0x11. Причина: `play()` с не-NULL объектом заходит
в ветку `if (Event::getCategory(...) == OK)`, а та делает vtable-вызов по
`local_40` (мусор) → косвенный переход в .rodata, выполнение строк → крах.

**Урок (записан в код):** возвращать движку «живые» FMOD-объекты нельзя —
виртуальные вызовы должны стыковаться с реальной раскладкой FMOD-заголовков,
которой у нас нет. Стабы обязаны отдавать **NULL** для всех out-объектов.

### 11.4. Итоговый фикс (в коде + в сборке)

1. `fmodevent_stub.cpp` — все геттеры: `*out = NULL`, `FMOD_OK`; только
   `FMOD_EventSystem_Create` возвращает псевдо-NULL (`(FMOD_SYSTEM*)1`).
   Убраны fake-объекты.
2. Движок, `FModSound::stopAllSoundFXEvents` @0x92DAC **заменён на no-op**
   (`movs r0,#0; bx lr` — байты `00 20 70 47`). Это единственное место,
   где NULL-слот разыменовывался. Патч **вшит в `tools/build-native.sh`**
   (после patch-versions) с проверкой ожидаемых байт (`b0 b5 02 af`) —
   при несовпадении сборка падает, а не портит бинарник.
3. Пересобрано, воспроизведение на станции — **упадков не было**,
   игра стабильна 20+ минут. Диалоги мгновенно проскакивают (см. 11.5).

### 11.5. Открытая задача — диалоги «проскакивают»

- Механизм: движок держит реплику, пока у реплики «играет звук»:
  `AESoundRessource::isPlaying(sid)` → `FModSound::isPlaying(sid)` →
  слота == NULL (а мы ставим NULL!) → `getState` недостижим → `isPlaying()`
  сразу `false` → следующая реплика.
- Запланированный план (стабы, движок не трогаем):
  1. `getEventBySystemID` → НЕ-NULL fake `Event` (слот не пуст) + `FMOD_OK`;
  2. `Event::getCategory` → код ОШИБКИ (≠0) → `play()` уходит в else-ветку,
     где слот ещё NULL → опасный vtable-вызов (краш №2) НЕ выполняется;
  3. `Event::getState(unsigned*)` — счётчик опросов, отдавать `8` (bit3 =
     PLAYING) первые ~180 опросов после `getEventBySystemID` (~3 с @60fps),
     затем 0 — реплика «додержится»;
  4. `Event::stop/start` сбрасывают таймер (клик «вперёд» не даёт висеть).
- Точный тест — станция + диалоги; окно времени крутить константой в стабе
  (не в движке!), потому что в стабе нет надёжного `gettimeofday` (shim-libc
  не экспортит) — счётчик опросов не требует новых импортов и версионезависим.

### 11.6. Инструменты отладки, которые реально пригодились

- Краш-хендлер хоста (`crash_handler` + `dump_module_maps`) пишет в stderr
  рега+карты — этого хватило для определения всех крашей без GDB.
- Ghidra 12.1.2 (ПК): `support/analyzeHeadless /tmp/opencode/ghidra_crashproj
  -import libgof2hdaa.so -scriptPath ... DecompileFMod.java` →
  полный декомпил всех 40+ методов FModSound в один txt; в том числе
  перепроверка оффсетов крашей и раскладки слотов.
- Дизасм Thumb-областей движка: `llvm-objdump -d --triple=thumbv7-linux-gnueabihf`;
  нужные куски вырезать `dd` + `llvm-objcopy -I binary --rename-section` в .text.
- `md5sum` сверка бинарников ПК↔устройство перед анализом (и патчем).

## 12.5. FMOD-аудио: РАБОТАЕТ (2026-08-09)

Звук заведён: реальные `libfmodex.so`/`libfmodevent.so` (4.44.07) из APK +
фейковый OpenSL ES + preload-хелперы. Что понадобилось:

1. **e_flags** у FMOD-либ: softfp→hardfp (как у движка). Без этого hardfp
   загрузчик не грузит их.
2. **`cpuinfo_fake.so`** (LD_PRELOAD, подмена open("/proc/cpuinfo")): FMOD
   читает cpuinfo и без строк `neon vfp vfpv3` выбирает недоступный код —
   `EventSystem::init`/`SetOutput` возвращают 48 (NOTREADY).
3. **`pthread_bionic.so`** (LD_PRELOAD, трансляция bionic pthread/sem/attr):
   bionic pthread_mutex_t=4B vs glibc 24B. glibc, писать в 4-байтный слот,
   затирает соседние структуры FMOD → краши.
4. **`FMOD_Memory_Initialize(NULL,0,malloc,realloc,free,...)`** из хоста:
   у FMOD свой внутренний пул-аллокатор, который в игре возвращал NULL на
   `FMOD_EventSystem_Create` (падение в `FUN_0004dba0`). Переключение на
   glibc malloc убирает падение полностью.
5. **`libOpenSLES.so`** — фейк OpenSL ES (opensl-выход — дефолтный для FMOD).
   Критичные ABI-детали (найдены методом проб на устройстве):
   - Хендлы OpenSL — **указатель-на-указатель**: `(*obj)->M` компилируется
     в `**obj` затем `->M`. slCreateEngine/CreateOutputMix/CreateAudioPlayer/
     GetInterface обязаны возвращать АДРЕС переменной-указателя, держащей
     адрес структуры (иначе FMOD прыгает в первый dword функции → SIGBUS).
   - Этот FMOD передаёт SLInterfaceID как 32-битное ЗНАЧЕНИЕ первого слова
     константы (0x2e=bufferqueue, 0x2f=play, 0x28=volume, 0x2b=androidcfg,
     0x80=androidsimplebufferqueue). Сравнивать memcmp'ом нельзя (краш).
   - Раскладка SLEngineItf у этого билда: CreateAudioPlayer@2,
     CreateOutputMix@7 (не как в стоковом OpenSL ES). Все слоты заполнены
     no-op, чтобы любой индекс был безопасен.
   - BufferQueue Enqueue → `SDL_QueueAudio`, pacing-тред зовёт callback FMOD
     пока очередь не опустела. `SDL_AUDIODRIVER=alsa`.
6. **libstdc++.so**: bionic-стаб (`__cxa_*`, `_ZdlPv`, `__aeabi_atexit`) —
   обязателен `extern "C"` (FMOD ссылается на НЕзамангленные имена
   `__cxa_pure_virtual` и т.п.).

`start-game.sh` ставит `LD_PRELOAD="cpuinfo_fake.so:pthread_bionic.so"` и
`SDL_AUDIODRIVER=alsa`; `build-native.sh` копирует реальные FMOD из `base.apk`
(патч e_flags) и собирает все хелперы.

Осталось открытым: переход игры в главное меню иногда затягивается
(не связано с аудио-инициализацией), прогресс по кампании не проверен.

## 12. TODO / открытое

- [ ] Реализовать план 11.5 (задержка реплик ~3 с) и протестировать на станции.
- [ ] Живой backtrace новой палицы: SIGILL блокирует gdbserver-сессию
      (программа зависает на ld.so при запуске под ptrace); кандидаты —
      core-dump через core_pattern (на устройстве readonly), либо разбор
      самого SIGILL-места в ld.so при трассировке.
- [ ] Диалоги: проверить клик-вперёд (A/B) при задержке реплики.