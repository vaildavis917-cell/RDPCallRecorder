# RDP Call Recorder v2.0 — Инструкция по сборке

## Что это

Лёгкий агент для автоматической записи звонков WhatsApp, Telegram и Viber в RDP-сессиях.

**Как работает:**
1. Админ устанавливает `RDPCallRecorder_Setup.exe` на сервер
2. При входе каждого юзера в RDP — агент стартует автоматически
3. Агент работает в фоне, мониторит WhatsApp/Telegram/Viber
4. Обнаруживает звонок → начинает запись в MP3
5. Звонок закончился → останавливает запись, ждёт следующий
6. Файлы сохраняются: `D:\CallRecordings\{Юзер}\{Дата}\{Дата}_{Юзер}_{Приложение}_{Время}.mp3`

---

## Требования для сборки

- **Windows 10/11** или **Windows Server 2019/2022**
- **Visual Studio 2022/2026 Community** (с компонентом "Разработка классических приложений на C++")
- **CMake** 3.15+ (обычно идёт с Visual Studio)
- **Git** (для клонирования AudioCapture)
- **NSIS** (для создания установщика) — https://nsis.sourceforge.io/Download

---

## Шаг 1: Клонировать AudioCapture

Откройте **Developer Command Prompt for VS** или обычный cmd:

```cmd
cd C:\Users\User\Documents\Projects
git clone https://github.com/masonasons/AudioCapture.git
```

Должна появиться папка `C:\Users\User\Documents\Projects\AudioCapture`.

---

## Шаг 2: Распаковать RDPCallRecorder

Распакуйте содержимое этого архива рядом с AudioCapture:

```
C:\Users\User\Documents\Projects\
├── AudioCapture\          ← клонированный репозиторий
└── RDPCallRecorder_v2\    ← этот проект
    ├── CMakeLists.txt
    ├── config.ini
    ├── src\
    │   ├── main.cpp
    │   ├── OpusEncoder_stub.cpp
    │   └── FlacEncoder_stub.cpp
    ├── include\
    │   ├── OpusEncoder.h   (заглушка)
    │   └── FlacEncoder.h   (заглушка)
    └── installer\
        └── installer.nsi
```

---

## Шаг 3: Сборка через CMake

### Вариант A: Через Developer Command Prompt (рекомендуется)

```cmd
cd C:\Users\User\Documents\Projects\RDPCallRecorder_v2

:: Создаём папку сборки
mkdir build
cd build

:: Конфигурация (Ninja — быстрее, но можно и без него)
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DAUDIOCAPTURE_DIR=C:\Users\User\Documents\Projects\AudioCapture

:: Сборка
cmake --build . --config Release
```

> **Примечание:** Если у вас Visual Studio 2026, замените `"Visual Studio 17 2022"` на правильный генератор. Можно также использовать Ninja:
> ```cmd
> cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DAUDIOCAPTURE_DIR=C:\Users\User\Documents\Projects\AudioCapture
> cmake --build .
> ```

### Вариант B: Через Visual Studio GUI

1. Откройте Visual Studio
2. File → Open → CMake... → выберите `CMakeLists.txt`
3. В настройках CMake укажите `AUDIOCAPTURE_DIR` = путь к AudioCapture
4. Build → Build All

---

## Шаг 4: Результат сборки

После успешной сборки файл будет в:
```
build\bin\Release\RDPCallRecorder.exe    (или build\bin\RDPCallRecorder.exe для Ninja)
```

Проверьте, что exe создан:
```cmd
dir build\bin\Release\RDPCallRecorder.exe
```

---

## Шаг 5: Создание установщика (NSIS)

1. Установите NSIS: https://nsis.sourceforge.io/Download

2. Создайте папку для файлов установщика:
```cmd
mkdir installer\files
copy build\bin\Release\RDPCallRecorder.exe installer\files\
copy config.ini installer\files\
```

3. Скомпилируйте установщик:
```cmd
cd installer
"C:\Program Files (x86)\NSIS\makensis.exe" installer.nsi
```

4. Результат: `installer\RDPCallRecorder_Setup.exe`

---

## Шаг 6: Установка на сервер

1. Скопируйте `RDPCallRecorder_Setup.exe` на Windows Server
2. Запустите **от имени администратора**
3. Установщик:
   - Копирует файлы в `C:\Program Files\RDPCallRecorder\`
   - Создаёт папку `D:\CallRecordings`
   - Прописывает автозапуск для всех пользователей (HKLM)
4. **Готово!** При следующем входе любого юзера в RDP — агент запустится автоматически

---

## Удаление

Через "Панель управления" → "Программы и компоненты" → "RDP Call Recorder" → Удалить.

Или запустите `C:\Program Files\RDPCallRecorder\Uninstall.exe`.

> **Записи НЕ удаляются** при деинсталляции (папка `D:\CallRecordings` сохраняется).

---

## Настройка (config.ini)

Файл `config.ini` лежит рядом с exe. Основные параметры:

| Параметр | Значение | Описание |
|----------|----------|----------|
| `RecordingPath` | `D:\CallRecordings` | Папка для записей |
| `AudioFormat` | `mp3` | Формат (mp3 или wav) |
| `MP3Bitrate` | `128000` | Битрейт MP3 |
| `PollInterval` | `2` | Интервал проверки (сек) |
| `SilenceThreshold` | `3` | Циклов тишины до остановки |
| `TargetProcesses` | `WhatsApp.exe,Telegram.exe,Viber.exe` | Процессы для мониторинга |
| `HideConsole` | `true` | Скрыть окно |
| `ProcessPriority` | `BelowNormal` | Приоритет (минимальная нагрузка) |
| `AutoRegisterStartup` | `true` | Саморегистрация в автозапуск |

---

## Формат имён файлов

```
D:\CallRecordings\
└── Ivanov\
    └── 2026-02-06\
        ├── 2026-02-06_Ivanov_WhatsApp_14-30-25.mp3
        ├── 2026-02-06_Ivanov_Telegram_15-45-10.mp3
        └── 2026-02-06_Ivanov_Viber_16-20-00.mp3
```

---

## Логи

Логи агента: `D:\CallRecordings\{Username}\logs\agent.log`

Для отладки установите `LogLevel=DEBUG` в config.ini.

---

## Устранение проблем

**Агент не записывает звонки:**
- Убедитесь, что Windows Server версии 2019+ (Build 19041+) — нужен для per-process audio capture
- Проверьте логи: `D:\CallRecordings\{Username}\logs\agent.log`
- Убедитесь, что WhatsApp/Telegram/Viber действительно используют аудио (не текстовый чат)

**Ошибки компиляции:**
- Убедитесь, что `AUDIOCAPTURE_DIR` указывает на правильную папку
- Убедитесь, что установлен компонент "Разработка классических приложений на C++" в Visual Studio

**Установщик не создаётся:**
- Убедитесь, что NSIS установлен и `makensis.exe` доступен
- Убедитесь, что файлы `RDPCallRecorder.exe` и `config.ini` лежат в `installer\files\`
