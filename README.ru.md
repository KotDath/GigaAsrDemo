# GigaAsrDemo

Демо-приложение для Aurora OS / Sailfish-based `Qt 5.6`, показывающее speech-to-text на bundled-модели GigaAM.

## Что делает приложение

- по первому нажатию кнопки начинает запись с микрофона
- по второму нажатию останавливает запись
- запускает офлайн-распознавание через GigaAM v3 RNNT
- показывает распознанный текст над кнопкой

## Техническая основа

- структура проекта скопирована с `OnnxRunner`
- интеграция ONNX Runtime оставлена в том же виде, что и в `OnnxRunner`:
  - `conanfile.py`
  - `pkgconfig(onnxruntime)`
  - `conan-deploy-libraries` в RPM spec
- интерфейс и рантайм совместимы с Aurora OS на базе Sailfish OS и `Qt 5.6`

## Bundled-файлы модели

Приложение ожидает следующие файлы в `models/gigaam-v3-e2e-rnnt`:

- `v3_e2e_rnnt_encoder.int8.onnx`
- `v3_e2e_rnnt_decoder.int8.onnx`
- `v3_e2e_rnnt_joint.int8.onnx`
- `v3_e2e_rnnt_vocab.txt`

После установки приложение ищет их по пути:

```text
/usr/share/ru.auroraos.GigaAsrDemo/models/gigaam-v3-e2e-rnnt
```

## Особенности сборки

Проект сохраняет подход `OnnxRunner` к пакетированию и добавляет одну Qt-зависимость:

- `Qt5Multimedia`

Для доступа к записи аудио приложение запрашивает:

- `Permissions=Microphone`
