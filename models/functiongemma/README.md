FunctionGemma GGUF model
========================

Place the merged and quantized FunctionGemma GGUF file for command execution in
this directory before packaging the application.

Expected runtime search path:

- `/usr/share/ru.auroraos.GigaAsrDemo/models/functiongemma/`

Preferred file name:

- `functiongemma-q8_0.gguf`

If several `GGUF` files are present, the app prefers `Q8_0` and falls back to
the first readable `*.gguf`.
