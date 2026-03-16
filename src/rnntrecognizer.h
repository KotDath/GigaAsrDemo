// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "audiobuffer.h"

struct ModelLayout {
    std::string modelDir;
    std::string encoder;
    std::string decoder;
    std::string joint;
    std::string vocab;
};

class RnntRecognizer
{
public:
    struct TensorNameCache {
        std::vector<std::string> storage;
        std::vector<const char *> ptrs;
    };

    struct EncoderOutput {
        std::vector<float> frames;
        int64_t timeSteps = 0;
        int64_t featureDim = 0;
    };

    struct DecoderState {
        std::vector<float> h;
        std::vector<float> c;
    };

    explicit RnntRecognizer(const ModelLayout &modelLayout);
    std::string Recognize(const AudioBuffer &audio) const;

private:
    std::vector<std::string> m_idToToken;
    int m_blankId = -1;
    Ort::Env m_env;
    Ort::SessionOptions m_sessionOptions;
    std::unique_ptr<Ort::Session> m_encoder;
    std::unique_ptr<Ort::Session> m_decoder;
    std::unique_ptr<Ort::Session> m_joint;
    TensorNameCache m_encoderInputs;
    TensorNameCache m_encoderOutputs;
    TensorNameCache m_decoderInputs;
    TensorNameCache m_decoderOutputs;
    TensorNameCache m_jointInputs;
    TensorNameCache m_jointOutputs;
};
