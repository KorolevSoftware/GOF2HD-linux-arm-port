# GOF2HD Linux Port — звук (FMOD): проблема, диагностика, фикс, остаток

Дата: 2026-08-09 — 2026-08-10
Статус: **ЗВУК РАБОТАЕТ** (музыка/звуки/диалоги). Найдены и исправлены три
независимые проблемы: bionic `FILE` в FEV-загрузчике, расположение FSB-банков
и несовместимый ABI математических вызовов FMOD. Игра стабильно рендерит и
озвучивается, крашей нет.

---

## 1. Исходная задача

В порте GOF2HD на Linux (ANBERNIC, armhf) не играла **игровая музыка / звуки**.
Симптом: `FMOD::EventSystem::load` возвращал **ошибку 22** (`FMOD_ERR_FILE_EOF`),
события не создавались, слоты событий пустые → при заходе на станцию движок
разыменовывал NULL (`stopAllSoundFXEvents`) и падал.

## 2. Что оказалось причиной ошибки 22 (корень)

Гипотеза из ранних заметок («64-битный off_t», FMOD читает `ftell()` как r0:r1)
**не подтвердилась**. Дизассемблер (`llvm-objdump -d --triple=thumbv7-...`) показал,
что FMOD использует **обычный 32-битный ABI** для `fopen/fread/fseek/ftell`
(`mov r1,#0; mov r2,#2; bl fseek`, возврат `ftell` кладётся в `r0`).

Реальная причина — **bionic FILE layout**:

```asm
; libfmodex.so, функция чтения @0xb85d0 (та же в libfmodevent @0x53ea4):
  bl   fread@plt        ; fread(ptr, 1, n, FILE*)
  ldrh r3, [r4, #12]    ; r3 = *(short*)(FILE + 12)  <-- bionic __sFILE._flags
  str  r0, [r5]         ; *bytesread = ret
  tst  r3, #32          ; __SEOF бит
  bne  -> FMOD_ERR_FILE_EOF (22)
  ands r0, r3, #64      ; __SERR бит
  movne r0, #19         ; FMOD_ERR_FILE_BAD (19)
  pop  {r4,r5,r6,pc}    ; return OK
```

FMOD (Android bionic-сборка) после `fread()` **напрямую читает bionic `_flags` из
`FILE+12`**. Когда FMOD связан с **glibc**, `FILE+12` = `_IO_read_base`
(указатель) → в битах `__SEOF(0x20)` / `__SERR(0x40)` случайный мусор →
ложный `FMOD_ERR_FILE_EOF(22)` / `FILE_BAD(19)`.

Движок (тоже bionic) этой проблемы не имел, потому что его stdio-импорты
`fopen/fread/fseek/ftell@LIBC` привязаны к нашему shim `libc.so` (bionic FILE-пул),
а FMOD-импорты были **unversioned** → биндились к glibc первыми в scope.

## 3. Решение: `port/fmodex-stub/libfmod_stdio.so`

Caller-aware LD_PRELOAD, перехватывает `fopen/fread/fseek/ftell/fclose` и
маршрутизирует по **адресу возврата** (`/proc/self/maps` через `open/read`):

| Вызывающий (по maps) | Куда идёт stdio |
|---|---|
| `libgof2hdaa.so` (движок) | shim `libc.so` (bionic FILE) |
| `libfmodex.so`, `libfmodevent.so` (FMOD) | shim `libc.so` (bionic FILE) |
| host, SDL2, ALSA, всё прочее | glibc (`RTLD_NEXT`) |

Это в точности повторяет раскладку, которая была у движка и без прелоада
(shim предшествует glibc в scope движка) — поэтому движок не ломается, а FMOD
получает корректные bionic `_flags` и FEV читается.

Важные технические детали:
- **Нельзя `dladdr`** внутри interposed stdio на раннем старте — валит движок.
  Только парсинг `/proc/self/maps` (`read()` в цикле до EOF — один `read()` не
  читает весь maps, иначе libfmodevent не находился).
- `resolve_shim` использует `dlopen(..., RTLD_NOLOAD)` + `dlvsym(h,"fopen","LIBC")`.
  **Нельзя** повторно `dlopen` shim: второй инстанс перезапускает constructor и
  обнуляет `__sF`-пул → ломается libzip.
- `RTLD_NEXT` для не-FMOD тоже должен отдавать glibc (проверено адресно).

Изменённые файлы:
- `port/fmodex-stub/libfmod_stdio.c` — новый (решение).
- `port/tools/build-native.sh` — сборка `libfmod_stdio.so`.
- `port/tools/start-game.sh` — `libfmod_stdio.so` первым в `LD_PRELOAD`.
- `GOF2HD_PORT_NOTES.md` — §12.5.2 (описание решения).

Дополнительно найденный нюанс пути FEV:
`FModSound::init()` строит `appRootDir + "FMOD_GOF2.fev"` (lowMemory=1). При
`appRootDir=/root/gof2hd/data` (без слэша) получается
`/root/gof2hd/dataFMOD_GOF2.fev` — файл с таким именем должен существовать
(создана копия; либо appRootDir должен оканчиваться на `/`).

## 4. Промежуточное состояние: FEV грузится, но звука нет (шум, скип диалогов)

После фикса:
- `FMOD::EventSystem::load` → **OK** (проект загружается).
- OpenSL-фейк активно заполняет буферы (`[opensl] bq.Enqueue` — тысячи).
- Игра стабильно работает (логотип→меню, без крашей), FMOD не падает.

**НО реального звука в динамиках нет**: слышен только шум/помехи, а диалоги
проскакивают. Причина скипа диалогов (известный механизм движка):

```
реплика держится, пока AESoundRessource::isPlaying(sid)
  -> FModSound::isPlaying(sid)
     -> events[sid] != NULL && Event::getState(&s) && (s >> 3) & 1
```

Если событие `events[sid]` пустое или `getState` не возвращает бит PLAYING,
движок считает реплику «отыгранной» и мгновенно переходит к следующей.
То есть события либо не создаются (`getEventBySystemID` → NULL), либо не
запускаются/не «звучат» — аудио-данные не декодируются/не проигрываются.

### Гипотезы, почему звук не воспроизводится (требует проверки)

1. **FSB-банки не читаются / не загружаются.** FEV ссылается на `.fsb`
   (сэмплы), лежащие рядом (`/root/gof2hd/data/audio/*.fsb`). Если FMOD не
   находит/не читает их (путь? доступ? чтение через shim?), события не получают
   аудио-данные → `getEventBySystemID` может вернуть событие, но `start()`
   не даёт звука; декодер выдаёт шум.
2. **OpenSL-выход отдаёт мусор.** `libOpenSLES.c` отдаёт в SDL/ALSA буферы,
   которые FMOD заполняет. Если FMOD реально не микширует данные (события не
   играют), в ALSA уходит тишина/шум — `bq.Enqueue` это просто «порция буфера»,
   не признак полезного звука.
3. **События создаются, но `getState` не PLAYING**, потому что `EventSystem::update`
   / mixer не крутится, либо `setPitch`/параметры валят.

### Что проверять дальше (план)

- Проверить, открывает ли FMOD `.fsb` файлы (трейс fopen по `.fsb` через
  libfmod_stdio) и успешно ли читает их.
- Проверить, что `FMOD::EventSystem::update` вызывается движком (в
  `FModSound::updateAll` каждые кадр) и что OpenSL-очередь действительно
  получает ненулевые PCM-данные (дамп буфера из `bq_Enqueue`).
- Проверить `getEventBySystemID`/`Event::start`/`Event::getState` возвращаемые
  коды (добавить печать FMOD_RESULT в движок нельзя — это бинарь; можно в
  трейсе через точки возврата).
- Сравнить с рабочим «лого-звуком»: в начале сессии при стоковой конфигурации
  был слышен именно логотипный звук (bq шёл и слышался). Значит, базовая
  цепочка OpenSL→ALSA выводила звук — проблема именно в данных FMOD (FSB).

## 4.1. НАЙДЕНО И ИСПРАВЛЕНО (2026-08-10): FSB-банки лежали не там

Диагностика (расширенный `fmod_load_test`): после Load запрашивали события
`FMOD_EventSystem_GetEventBySystemID(sid, mode, &ev)` и стартовали их.

**mode=4 (INFOONLY)**: события возвращались, но `Event_Start` → 84
(`FMOD_ERR_EVENT_INFOONLY`) — «start» на информационном хендле запрещён.
**mode=0**: `GetEventBySystemID` → 23 (`FMOD_ERR_FILE_NOTFOUND`), события не
создаются. При этом в трейсе stdio было:

```
shim_fopen /root/gof2hd/FMOD_GOF2_SFX_SPACE.fsb FAILED errno=2
```

FMOD ищет FSB-банки **в каталоге FEV**. FEV-путь (из-за отсутствия слэша) —
`/root/gof2hd/dataFMOD_GOF2.fev`, значит FSB ищутся в `/root/gof2hd/`. А реальные
банки лежали в `/root/gof2hd/data/audio/`.

**Решение:** скопировать все `*.fsb` из `/root/gof2hd/data/audio/` в
`/root/gof2hd/` (каталог FEV). После этого:

```
fopen /root/gof2hd/FMOD_GOF2_SFX_SPACE.fsb fmod=1 -> 0xf742454c   (OK, через shim)
GetEventBySystemID(0, mode=0) -> 0 (OK) ev=0x1
  Event_Start  -> 0 (OK)
  Event_GetState -> st=0x18   (bit3=0x08 PLAYING, bit4=0x10)
```

События создаются и **проигрываются**. В игре FMOD стал открывать
`FMOD_GOF2_MUSIC.fsb` и `FMOD_GOF2_SFX_SPACE.fsb`, звук идёт, диалоги не
скипаются.

Автоматизация: `start-game.sh` теперь сам копирует `.fsb` из
`$GOF_ROOT/data/audio/` в `$GOF_ROOT/` (где живёт `dataFMOD_GOF2.fev`), если их
там нет.

> Итоговый «рецепт»: (1) `libfmod_stdio.so` в `LD_PRELOAD` (FMOD/engine → shim,
> остальное → glibc), (2) файл `$GOF_ROOT/dataFMOD_GOF2.fev` (или appRootDir с
> завершающим `/`), (3) `*.fsb` рядом с FEV.

## 5. Финальная причина: softfp FMOD вызывал hardfp `libm`

После исправления путей и stdio события уже создавались, но музыка звучала с
треском, а реплики либо не были слышны, либо не завершались корректно. Это не
оказалось проблемой FSB, OpenSL или ALSA.

### Как локализовали

1. На подключённом Android-планшете из оригинальной `libfmodex.so` прочитали
   первый subsound из `FMOD_GOF2_MUSIC.fsb`. Он дал `131051` ненулевой
   16-битный PCM-сэмпл из `131072`, диапазон `[-27401,25528]`.
2. SHA-256 FSB на Android и на консоли совпал. Тот же POSIX callback
   `open/seek/read` на планшете дал тот же PCM, поэтому формат банка и
   файловый backend исключили.
3. На консоли изолированный FMOD сначала возвращал тишину, затем после первых
   ABI-обёрток - только несколько тысяч ненулевых сэмплов. Это указывало на
   ошибку именно внутри декодера, до OpenSL.
4. `LD_DEBUG=bindings` показал, что unversioned math-импорты Android FMOD
   (`sqrt`, `log`, `sin` и другие) привязываются напрямую к hardfp glibc
   `libm.so.6`, минуя локальный softfp shim.

### Исправление

`port/shim/libm.c` экспортирует softfp (`pcs("aapcs")`) входы для всех
math-импортов FMOD и через указатели `pcs("aapcs-vfp")` вызывает настоящую
hardfp `libm.so.6`. Та же библиотека нужна движку и загружается первой через
`LD_PRELOAD`; SDL/ALSA сохраняют свои versioned GLIBC-привязки и не получают
softfp-функции.

Критичная деталь: первоначально в мосте не было `double cos(double)`. У FMOD
этот импорт называется `cos`, а не `cosf`, то есть это именно `double`
вариант. Один такой вызов с неверным соглашением о передаче аргументов портил
декодер. После добавления `cos` изолированная проверка на консоли в точности
совпала с Android:

```
bytes=262144 range=[-27401,25528] nonzero=131051
```

После штатного запуска игры музыка, эффекты и реплики работают; диалоги снова
переключаются по реальному завершению воспроизведения.

### Рабочий runtime-набор

`start-game.sh` всегда загружает:

1. `libfmod_filesystem.so` - потокобезопасный POSIX backend FEV/FSB.
2. `cpuinfo_fake.so` и `pthread_bionic.so` - Android-совместимость FMOD.
3. `libm.so` - общий мост softfp игры и FMOD к hardfp glibc libm.

Тестовые программы, отладочные preloads и экспериментальные режимы удалены
после подтверждения исправления на устройстве. Исторический
`libfmod_stdio.so` также удалён: после установки `FMOD_System_SetFileSystem`
FMOD больше не обращается к glibc `FILE`, а движок получает bionic stdio из
shim `libc.so` через свои `@LIBC`-импорты. Запуск без stdio-моста проверен на
консоли: музыка и реплики работают.

## 6. Как воспроизвести / проверить

```sh
# на устройстве
bash /root/gof2hd/port/tools/start-game.sh
# лог: tail -f /root/gof2hd/run.txt
# звук: встроенные динамики; A — тап (закрыть лого), B — назад
```

Полезные env для диагностики:
- `OPENSL_DEBUG=1` — лог OpenSL (SL create/enqueue).
- `GOF_FMOD_STDIO_DEBUG=1` — лог маршрутизации stdio (кто → shim/glibc).
- `GOF_TRACE=2` — трейс shim stdio.

---

## 7. ИТОГ — что было сделано (сводка)

### Проблема 1: FEV не грузится — `FMOD_ERR_FILE_EOF(22)`

**Причина.** FMOD собран под Android/bionic и после `fread()` читает bionic
`__sFILE._flags` напрямую из `FILE+12` (`ldrh r3,[r4,#12]; tst r3,#32` → `__SEOF`
→ 22; `ands r0,r3,#64` → 19). Когда FMOD связан с **glibc**, `FILE+12` —
это `_IO_read_base` (указатель) → случайные биты EOF/ERR → ложный
`FMOD_ERR_FILE_EOF(22)` / `FILE_BAD(19)`. Движок такой проблемы не имел, т.к. его
`fopen/fread/fseek/ftell@LIBC` биндились к нашему shim (bionic FILE-пул), а FMOD
(unversioned импорты) — к glibc.

**Решение.** Новый `port/fmodex-stub/libfmod_stdio.c` — caller-aware LD_PRELOAD,
перехватывает `fopen/fread/fseek/ftell/fclose` и маршрутизирует по адресу
возврата (`/proc/self/maps`, читается `open/read` в цикле до EOF):

| Вызывающий | Куда идёт stdio |
|---|---|
| `libgof2hdaa.so`, `libfmodex.so`, `libfmodevent.so` | shim `libc.so` (bionic FILE) |
| host, SDL2, ALSA, прочее | glibc (`RTLD_NEXT`) |

Технические детали:
- **`dladdr` нельзя** внутри interposed stdio на раннем старте — валит движок.
- `resolve_shim` = `dlopen(..., RTLD_NOLOAD)` + `dlvsym(h,"fopen","LIBC")`.
  Повторный `dlopen` shim запрещён (constructor обнуляет `__sF`-пул → ломается libzip).
- `RTLD_NEXT` для не-FMOD проверен и отдаёт glibc.

### Проблема 2: «звука нет — в динамиках шум, диалоги скипаются»

**Симптом.** После фикса ошибки 22 FEV загружается, OpenSL-буферы
(`bq.Enqueue`) идут тысячами, но реального звука нет — шум/тишина, а реплики
диалогов проскакивают (движок не знает длительности: реплика держится только
пока `FModSound::isPlaying(sid)` → `Event::getState` даёт бит `PLAYING(0x08)`).

**Причина.** FMOD ищет FSB-банки (`*.fsb`, сэмплы) **в каталоге FEV**.
FEV-путь из-за отсутствия слэша в `appRootDir` = `/root/gof2hd/dataFMOD_GOF2.fev`,
значит FSB ищутся в `/root/gof2hd/`. Реальные банки лежали в
`/root/gof2hd/data/audio/`. Без них:
- `FMOD_EventSystem_GetEventBySystemID(sid, mode=0, ...)` → **23 (FILE_NOTFOUND)**,
  события не создаются;
- режим `mode=4` (INFOONLY) → события есть, но `Event_Start` → **84
  (`FMOD_ERR_EVENT_INFOONLY`)**;
- в динамики уходит мусор/тишина, диалоги скипаются.

**Решение.** Скопировать все `*.fsb` из `/root/gof2hd/data/audio/` в
`/root/gof2hd/` (каталог FEV). `start-game.sh` теперь делает это автоматически
перед запуском. После этого:

```
fopen /root/gof2hd/FMOD_GOF2_SFX_SPACE.fsb fmod=1 -> 0xf742454c   (OK)
GetEventBySystemID(0, mode=0) -> 0 (OK) ev=0x1
  Event_Start    -> 0 (OK)
  Event_GetState -> st=0x18   ; bit3=0x08 PLAYING
```

События создаются и проигрываются; FMOD открывает `FMOD_GOF2_MUSIC.fsb` и
`FMOD_GOF2_SFX_SPACE.fsb`; музыка/звуки/диалоги работают.

### Рецепт (3 пункта)

1. `libfmod_stdio.so` — первым в `LD_PRELOAD` (build-native.sh собирает).
2. Файл `$GOF_ROOT/dataFMOD_GOF2.fev` (или `appRootDir` с завершающим `/`).
3. `*.fsb` рядом с FEV — `start-game.sh` копирует из `data/audio/`.

### Изменённые файлы

- `port/fmodex-stub/libfmod_stdio.c` — новый (решение).
- `port/tools/build-native.sh` — сборка `libfmod_stdio.so`.
- `port/tools/start-game.sh` — PRELOAD + авто-копирование `.fsb`.
- `GOF2HD_PORT_NOTES.md` — §12.5.2 (описание решения).
- `sound.md` — этот отчёт.

### Проверка на устройстве

Игра стабильно работает (логотип→меню, тысячи `[opensl] bq.Enqueue`), крашей
нет, звук идёт. Консоль: `bash /root/gof2hd/port/tools/start-game.sh`.
