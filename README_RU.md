# RDP Call Recorder

**Агент автоматической записи звонков в RDP-сессиях.** Записывает звонки WhatsApp, Telegram и Viber с голосами обоих собеседников в один аудиофайл.

![RDP Call Recorder](src/app.ico)

## Возможности

- **Автоматическое обнаружение звонков** — мониторит процессы WhatsApp, Telegram и Viber на наличие активных аудиосессий
- **Запись обоих голосов** — захватывает микрофон (ваш голос) и аудиовыход приложения (голос собеседника) через микширование
- **Фоновая работа** — работает невидимо в системном трее, не требует взаимодействия
- **Автозапуск** — при первом запуске прописывается в автозагрузку Windows
- **GUI настроек** — можно в любой момент изменить папку записей, формат аудио, список процессов и другие параметры
- **Умное именование файлов** — `{Дата}_{RDP-пользователь}_{Приложение}_{Время}.mp3` (например, `2026-02-06_Ivanov_WhatsApp_14-30-25.mp3`)
- **Поддержка RDP** — мониторит только процессы текущей RDP-сессии
- **Пользовательская установка** — не требует прав администратора, ставится в папку пользователя
- **NSIS-установщик** — установка в один клик с ярлыком на рабочем столе и деинсталлятором
- **Гибкая настройка** — все параметры хранятся в `config.ini`

## Как это работает

1. Агент запускается и работает в фоне (иконка в системном трее)
2. Каждые 2 секунды проверяет, есть ли у WhatsApp/Telegram/Viber активная аудиосессия (идёт звонок)
3. При обнаружении звонка:
   - Запускает захват аудиовыхода приложения (голос собеседника)
   - Запускает захват микрофона (ваш голос)
   - Микширует оба потока в один MP3-файл
4. Когда звонок заканчивается (тишина 6 секунд), останавливает запись и сохраняет файл
5. Ждёт следующий звонок

## Установка

### Из установщика (рекомендуется)

1. Скачайте `RDPCallRecorder_Setup.exe` со страницы [Релизы](../../releases)
2. **Запустите установщик от имени обычного пользователя** (права администратора НЕ нужны!)
3. Программа установится в `%LOCALAPPDATA%\RDPCallRecorder\` (папка текущего пользователя)
4. На рабочем столе появится ярлык — кликните для открытия настроек
5. Выберите папку для записей (по умолчанию: `%USERPROFILE%\CallRecordings`) и нажмите "Сохранить"
6. Готово! Агент будет автоматически запускаться при каждом входе в RDP

### Важно: установка для каждого пользователя

Каждый RDP-пользователь должен установить программу **самостоятельно** под своей учётной записью. Это гарантирует:
- Все папки и файлы принадлежат пользователю (нет проблем с правами доступа)
- Автозапуск работает для конкретного пользователя
- Записи сохраняются в профиле пользователя
- Не требуются права администратора

### Из исходников

Смотрите раздел [Сборка из исходников](#сборка-из-исходников) ниже.

## Настройки

Двойной клик по иконке в трее или по ярлыку на рабочем столе открывает окно настроек:

| Параметр | Описание | По умолчанию |
|----------|----------|--------------|
| Папка записей | Куда сохранять записи | `%USERPROFILE%\CallRecordings` |
| Формат аудио | MP3 или WAV | MP3 |
| Битрейт MP3 | Качество аудио (кбит/с) | 128 |
| Целевые процессы | Список через запятую | `WhatsApp.exe, Telegram.exe, Viber.exe` |
| Интервал опроса | Как часто проверять звонки (секунды) | 2 |
| Порог тишины | Сколько пустых опросов до остановки | 3 |
| Логирование | Вести файл лога | Да |
| Автозапуск | Регистрация в автозагрузке Windows | Да |

## Структура файлов

Записи организованы следующим образом:
```
%USERPROFILE%\CallRecordings\
  {ИмяПользователя}\
    {Дата}\
      2026-02-06_Ivanov_WhatsApp_14-30-25.mp3
      2026-02-06_Ivanov_Telegram_15-45-10.mp3
    logs\
      agent.log
```

Программа установлена в:
```
%LOCALAPPDATA%\RDPCallRecorder\
  RDPCallRecorder.exe
  config.ini
  app.ico
  Uninstall.exe
```

## Сборка из исходников

### Требования

- **Windows 10/11 или Windows Server 2019+**
- **Visual Studio 2022/2026** с компонентом "Разработка классических приложений на C++"
- **CMake 3.20+**
- **NSIS** (опционально, для сборки установщика)

### Зависимости

Проект использует библиотеку [AudioCapture](https://github.com/masonasons/AudioCapture). Клонируйте её рядом с проектом:

```cmd
cd C:\Projects
git clone https://github.com/masonasons/AudioCapture.git
git clone https://github.com/vaildavis917-cell/RDPCallRecorder.git
```

### Шаги сборки

1. **Скопируйте заглушки заголовков** (чтобы не устанавливать Opus/FLAC):
```cmd
copy RDPCallRecorder\include\OpusEncoder.h AudioCapture\include\OpusEncoder.h
copy RDPCallRecorder\include\FlacEncoder.h AudioCapture\include\FlacEncoder.h
```

2. **Откройте x64 Native Tools Command Prompt for VS** и соберите:
```cmd
cd RDPCallRecorder
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DAUDIOCAPTURE_DIR=C:\Projects\AudioCapture
cmake --build .
```

3. **Сборка установщика** (опционально):
```cmd
mkdir ..\installer\files
copy bin\RDPCallRecorder.exe ..\installer\files\
copy ..\config.ini ..\installer\files\
cd ..\installer
"C:\Program Files (x86)\NSIS\makensis.exe" installer.nsi
```

## Конфигурационный файл

Файл `config.ini` находится рядом с исполняемым файлом (`%LOCALAPPDATA%\RDPCallRecorder\config.ini`):

```ini
[Recording]
; Оставьте пустым для автоопределения (%USERPROFILE%\CallRecordings)
RecordingPath=
AudioFormat=mp3
MP3Bitrate=128000

[Monitoring]
PollInterval=2
SilenceThreshold=3

[Processes]
TargetProcesses=WhatsApp.exe,Telegram.exe,Viber.exe

[Logging]
EnableLogging=true
LogLevel=INFO
MaxLogSizeMB=10

[Advanced]
HideConsole=true
AutoRegisterStartup=true
ProcessPriority=BelowNormal
```

## Системные требования

- Windows 10 Build 20348+ / Windows 11 / Windows Server 2019+
- Аудиоустройство с поддержкой WASAPI
- Микрофон (для записи вашего голоса)

## Примечание об антивирусе

Windows Defender может пометить приложение как `Behavior:Win32/Persistence.Alml`, потому что оно регистрируется в автозагрузке. Это **ложное срабатывание**. Добавьте исключение для папки установки:

```powershell
# Для пользовательской установки (нужны права админа для добавления исключения):
Add-MpPreference -ExclusionPath "$env:LOCALAPPDATA\RDPCallRecorder\"
```

Или попросите администратора RDP-сервера добавить исключение для всех пользователей:
```powershell
Add-MpPreference -ExclusionPath "C:\Users\*\AppData\Local\RDPCallRecorder\"
```

## Лицензия

Проект распространяется под лицензией MIT. Подробности в файле [LICENSE](LICENSE).

## Благодарности

- Библиотека [AudioCapture](https://github.com/masonasons/AudioCapture) от masonasons — захват аудио через WASAPI, микширование и кодирование
