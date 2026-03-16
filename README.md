# GigaAsrDemo

Aurora OS / Sailfish-based `Qt 5.6` demo application for speech-to-text with bundled GigaAM ONNX models.

## What it does

- starts microphone recording after the first button tap
- stops recording after the second tap
- runs offline recognition with GigaAM v3 RNNT
- shows the recognized text above the button

## Technical base

- project structure copied from `OnnxRunner`
- ONNX runtime is integrated the same way as in `OnnxRunner`:
  - `conanfile.py`
  - `pkgconfig(onnxruntime)`
  - `conan-deploy-libraries` in the RPM spec
- UI and runtime are kept compatible with Aurora OS on top of Sailfish OS and `Qt 5.6`

## Bundled model files

The app expects the following bundled files in `models/gigaam-v3-e2e-rnnt`:

- `v3_e2e_rnnt_encoder.int8.onnx`
- `v3_e2e_rnnt_decoder.int8.onnx`
- `v3_e2e_rnnt_joint.int8.onnx`
- `v3_e2e_rnnt_vocab.txt`

At runtime the installed path is:

```text
/usr/share/ru.auroraos.GigaAsrDemo/models/gigaam-v3-e2e-rnnt
```

## Build notes

The project keeps the same packaging approach as `OnnxRunner`, with one extra Qt dependency:

- `Qt5Multimedia`

The application also requests:

- `Permissions=Microphone`
