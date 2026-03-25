# GigaAsrDemo

Aurora OS / Sailfish-based `Qt 5.6` demo application for speech-to-text with bundled GigaAM ONNX models and a local FunctionGemma command stage.

## What it does

- starts microphone recording after the first button tap
- stops recording after the second tap
- runs offline recognition with GigaAM v3 RNNT
- shows the recognized text above the button
- sends the recognized text to a local FunctionGemma GGUF model
- tries to produce a single function call and execute it

## Technical base

- project structure copied from `OnnxRunner`
- ONNX runtime is integrated the same way as in `OnnxRunner`:
  - `conanfile.py`
  - `pkgconfig(onnxruntime)`
  - `conan-deploy-libraries` in the RPM spec
- llama.cpp is integrated for the command model stage
- UI and runtime are kept compatible with Aurora OS on top of Sailfish OS and `Qt 5.6`

## Bundled model files

The app expects the following bundled files in `models/gigaam-v3-e2e-rnnt`:

- `v3_e2e_rnnt_encoder.int8.onnx`
- `v3_e2e_rnnt_decoder.int8.onnx`
- `v3_e2e_rnnt_joint.int8.onnx`
- `v3_e2e_rnnt_vocab.txt`

The command stage expects a bundled GGUF model in:

- `models/functiongemma/*.gguf`

If multiple command-model files are present, the loader prefers:

- `functiongemma-q8_0.gguf`
- any other `*-q8_0.gguf`
- then the first readable `*.gguf` file in lexical order

At runtime the installed path is:

```text
/usr/share/ru.auroraos.GigaAsrDemo/models/gigaam-v3-e2e-rnnt
```

## Build notes

The project keeps the same packaging approach as `OnnxRunner`, with one extra Qt dependency:

- `Qt5Multimedia`

The application also requests:

- `Permissions=Microphone`
- `Permissions=ScreenCapture`

## Notes for the command stage

- The FunctionGemma runtime currently uses a manually constructed function-calling prompt.
- The bundled `GGUF` should be produced from the merged fine-tuned checkpoint with the original tokenizer assets preserved, otherwise `llama.cpp` falls back to the wrong tokenizer path and function calling quality degrades.
