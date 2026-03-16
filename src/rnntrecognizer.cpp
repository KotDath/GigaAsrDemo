// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include "rnntrecognizer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

constexpr int kTargetSampleRate = 16000;
constexpr int kFeatureBins = 64;
constexpr int kFftSize = 320;
constexpr int kHopSize = 160;
constexpr int kMaxTokensPerStep = 3;
constexpr float kPi = 3.14159265358979323846f;

std::vector<float> ResampleLinear(const std::vector<float> &input, int inputSampleRate, int outputSampleRate)
{
    if (input.empty()) {
        return {};
    }
    if (inputSampleRate == outputSampleRate) {
        return input;
    }

    const double ratio = static_cast<double>(outputSampleRate) / inputSampleRate;
    const size_t outputSize = static_cast<size_t>(input.size() * ratio);
    std::vector<float> output(outputSize);

    for (size_t i = 0; i < outputSize; ++i) {
        const double sourceIndex = static_cast<double>(i) / ratio;
        const size_t left = static_cast<size_t>(sourceIndex);
        const size_t right = std::min(left + 1, input.size() - 1);
        const double alpha = sourceIndex - left;
        output[i] = static_cast<float>((1.0 - alpha) * input[left] + alpha * input[right]);
    }

    return output;
}

RnntRecognizer::TensorNameCache collectNames(Ort::Session &session, bool inputs)
{
    RnntRecognizer::TensorNameCache cache;
    const size_t count = inputs ? session.GetInputCount() : session.GetOutputCount();
    cache.storage.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        auto name = inputs
                        ? session.GetInputNameAllocated(i, Ort::AllocatorWithDefaultOptions())
                        : session.GetOutputNameAllocated(i, Ort::AllocatorWithDefaultOptions());
        cache.storage.emplace_back(name.get());
    }

    cache.ptrs.reserve(cache.storage.size());
    for (size_t i = 0; i < cache.storage.size(); ++i) {
        cache.ptrs.push_back(cache.storage[i].c_str());
    }

    return cache;
}

std::vector<std::string> loadTokens(const std::string &filename)
{
    std::ifstream file(filename.c_str());
    if (!file) {
        throw std::runtime_error("Cannot open tokens file: " + filename);
    }

    std::vector<std::string> idToToken;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        const size_t split = line.find_last_of(" \t");
        if (split == std::string::npos || split + 1 >= line.size()) {
            throw std::runtime_error("Invalid tokens line: " + line);
        }

        const std::string token = line.substr(0, split);
        const int id = std::stoi(line.substr(split + 1));
        if (id < 0) {
            throw std::runtime_error("Negative token id in tokens file.");
        }

        if (static_cast<size_t>(id) >= idToToken.size()) {
            idToToken.resize(static_cast<size_t>(id) + 1);
        }
        idToToken[static_cast<size_t>(id)] = token;
    }

    if (idToToken.empty()) {
        throw std::runtime_error("Tokens file is empty: " + filename);
    }

    return idToToken;
}

int findBlankId(const std::vector<std::string> &idToToken)
{
    for (size_t i = 0; i < idToToken.size(); ++i) {
        if (idToToken[i] == "<blk>" || idToToken[i] == "<blank>") {
            return static_cast<int>(i);
        }
    }

    return static_cast<int>(idToToken.size()) - 1;
}

bool isBlankToken(const std::string &token)
{
    return token == "<blk>" || token == "<blank>";
}

bool isSpecialToken(const std::string &token)
{
    return isBlankToken(token) || token == "<unk>";
}

std::string collapseAsciiWhitespace(const std::string &text)
{
    std::string output;
    output.reserve(text.size());

    bool previousWasSpace = true;
    for (unsigned char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (!previousWasSpace) {
                output.push_back(' ');
            }
            previousWasSpace = true;
            continue;
        }

        output.push_back(static_cast<char>(ch));
        previousWasSpace = false;
    }

    if (!output.empty() && output.back() == ' ') {
        output.pop_back();
    }

    return output;
}

std::string cleanupDecodedTextSpacing(const std::string &text)
{
    static const std::string kClosingPunctuation = ",.!?:;)]}";

    std::string output;
    output.reserve(text.size());
    for (unsigned char ch : text) {
        if (!output.empty()
            && kClosingPunctuation.find(output.back()) != std::string::npos
            && ch != ' '
            && kClosingPunctuation.find(ch) == std::string::npos) {
            output.push_back(' ');
        }
        if (ch == ' ' && !output.empty() && kClosingPunctuation.find(output.back()) != std::string::npos) {
            continue;
        }
        if (kClosingPunctuation.find(ch) != std::string::npos && !output.empty() && output.back() == ' ') {
            output.pop_back();
        }
        output.push_back(static_cast<char>(ch));
    }

    return collapseAsciiWhitespace(output);
}

std::string printableToken(const std::string &token)
{
    if (isSpecialToken(token)) {
        return "";
    }

    if (token == "<space>" || token == "▁") {
        return " ";
    }

    std::string piece = token;
    size_t position = 0;
    while ((position = piece.find("▁", position)) != std::string::npos) {
        piece.replace(position, std::string("▁").size(), " ");
        position += 1;
    }
    return piece;
}

std::string decodeRnntTokens(const std::vector<int> &tokens, const std::vector<std::string> &idToToken)
{
    std::string text;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const int id = tokens[i];
        if (id < 0 || static_cast<size_t>(id) >= idToToken.size()) {
            throw std::runtime_error("Decoder produced token id outside vocabulary.");
        }
        text += printableToken(idToToken[static_cast<size_t>(id)]);
    }
    return cleanupDecodedTextSpacing(text);
}

double hzToMelHtk(double freq)
{
    return 2595.0 * std::log10(1.0 + freq / 700.0);
}

double melToHzHtk(double mel)
{
    return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
}

std::vector<float> buildMelFilterbank()
{
    const int numFreqs = kFftSize / 2 + 1;
    std::vector<float> fbanks(static_cast<size_t>(numFreqs) * kFeatureBins, 0.0f);

    std::vector<double> allFreqs(numFreqs);
    for (int i = 0; i < numFreqs; ++i) {
        allFreqs[static_cast<size_t>(i)] = static_cast<double>(i) * kTargetSampleRate / kFftSize;
    }

    const double melMin = hzToMelHtk(0.0);
    const double melMax = hzToMelHtk(8000.0);

    std::vector<double> melPoints(kFeatureBins + 2);
    for (int i = 0; i < kFeatureBins + 2; ++i) {
        const double alpha = static_cast<double>(i) / (kFeatureBins + 1);
        melPoints[static_cast<size_t>(i)] = melToHzHtk(melMin + (melMax - melMin) * alpha);
    }

    for (int f = 0; f < numFreqs; ++f) {
        const double hz = allFreqs[static_cast<size_t>(f)];
        for (int m = 0; m < kFeatureBins; ++m) {
            const double left = melPoints[static_cast<size_t>(m)];
            const double center = melPoints[static_cast<size_t>(m + 1)];
            const double right = melPoints[static_cast<size_t>(m + 2)];

            double value = 0.0;
            if (hz >= left && hz <= center && center > left) {
                value = (hz - left) / (center - left);
            } else if (hz > center && hz <= right && right > center) {
                value = (right - hz) / (right - center);
            }

            fbanks[static_cast<size_t>(f) * kFeatureBins + m] = static_cast<float>(std::max(0.0, value));
        }
    }

    return fbanks;
}

std::vector<float> buildPeriodicHannWindow()
{
    std::vector<float> window(kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos((2.0f * kPi * i) / kFftSize);
    }
    return window;
}

std::vector<float> computeFbank(const std::vector<float> &audio)
{
    if (audio.size() < static_cast<size_t>(kFftSize)) {
        throw std::runtime_error("Audio is too short for GigaAM preprocessing.");
    }

    const int64_t numFrames = 1 + static_cast<int64_t>((audio.size() - kFftSize) / kHopSize);
    if (numFrames <= 0) {
        throw std::runtime_error("Feature extractor produced zero frames.");
    }

    const std::vector<float> window = buildPeriodicHannWindow();
    const std::vector<float> melFbanks = buildMelFilterbank();
    const int numFreqs = kFftSize / 2 + 1;

    std::vector<float> cosTable(static_cast<size_t>(numFreqs) * kFftSize);
    std::vector<float> sinTable(static_cast<size_t>(numFreqs) * kFftSize);
    for (int k = 0; k < numFreqs; ++k) {
        for (int n = 0; n < kFftSize; ++n) {
            const float angle = 2.0f * kPi * k * n / kFftSize;
            cosTable[static_cast<size_t>(k) * kFftSize + n] = std::cos(angle);
            sinTable[static_cast<size_t>(k) * kFftSize + n] = -std::sin(angle);
        }
    }

    std::vector<float> features(static_cast<size_t>(numFrames) * kFeatureBins);
    std::vector<float> frame(kFftSize);
    std::vector<float> power(static_cast<size_t>(numFreqs));

    for (int64_t frameIndex = 0; frameIndex < numFrames; ++frameIndex) {
        const size_t offset = static_cast<size_t>(frameIndex) * kHopSize;
        for (int i = 0; i < kFftSize; ++i) {
            frame[static_cast<size_t>(i)] = audio[offset + static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        }

        for (int k = 0; k < numFreqs; ++k) {
            float real = 0.0f;
            float imag = 0.0f;
            for (int n = 0; n < kFftSize; ++n) {
                const float sample = frame[static_cast<size_t>(n)];
                real += sample * cosTable[static_cast<size_t>(k) * kFftSize + n];
                imag += sample * sinTable[static_cast<size_t>(k) * kFftSize + n];
            }
            power[static_cast<size_t>(k)] = real * real + imag * imag;
        }

        for (int bin = 0; bin < kFeatureBins; ++bin) {
            double energy = 1.0e-10;
            for (int k = 0; k < numFreqs; ++k) {
                energy += power[static_cast<size_t>(k)] * melFbanks[static_cast<size_t>(k) * kFeatureBins + bin];
            }
            features[static_cast<size_t>(frameIndex) * kFeatureBins + bin] = static_cast<float>(std::log(energy));
        }
    }

    return features;
}

std::vector<float> transposeFeaturesToBct(const std::vector<float> &features, int64_t numFrames)
{
    std::vector<float> transposed(static_cast<size_t>(numFrames) * kFeatureBins);
    for (int64_t t = 0; t < numFrames; ++t) {
        for (int c = 0; c < kFeatureBins; ++c) {
            transposed[static_cast<size_t>(c) * numFrames + static_cast<size_t>(t)] =
                features[static_cast<size_t>(t) * kFeatureBins + c];
        }
    }
    return transposed;
}

std::vector<float> copyTensorToVector(const Ort::Value &value)
{
    const auto info = value.GetTensorTypeAndShapeInfo();
    const size_t count = info.GetElementCount();
    const float *data = value.GetTensorData<float>();
    return std::vector<float>(data, data + count);
}

RnntRecognizer::EncoderOutput runEncoder(Ort::Session &encoder,
                                         const RnntRecognizer::TensorNameCache &inputNames,
                                         const RnntRecognizer::TensorNameCache &outputNames,
                                         const std::vector<float> &featuresBct,
                                         int64_t numFrames)
{
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<int64_t> featureShape = {1, kFeatureBins, numFrames};
    std::vector<int64_t> lengthShape = {1};
    int64_t lengthValue = numFrames;

    Ort::Value featuresTensor = Ort::Value::CreateTensor<float>(
        memoryInfo,
        const_cast<float *>(featuresBct.data()),
        featuresBct.size(),
        featureShape.data(),
        featureShape.size());

    Ort::Value lengthTensor = Ort::Value::CreateTensor<int64_t>(
        memoryInfo,
        &lengthValue,
        1,
        lengthShape.data(),
        lengthShape.size());

    std::vector<Ort::Value> inputs;
    inputs.emplace_back(std::move(featuresTensor));
    inputs.emplace_back(std::move(lengthTensor));

    auto outputs = encoder.Run(Ort::RunOptions{nullptr},
                               inputNames.ptrs.data(),
                               inputs.data(),
                               inputs.size(),
                               outputNames.ptrs.data(),
                               outputNames.ptrs.size());

    if (outputs.size() < 2) {
        throw std::runtime_error("RNNT encoder must return encoded tensor and encoded lengths.");
    }

    Ort::Value *encodedTensor = nullptr;
    Ort::Value *lengthTensorOut = nullptr;
    for (size_t i = 0; i < outputs.size(); ++i) {
        auto &output = outputs[i];
        const auto info = output.GetTensorTypeAndShapeInfo();
        const auto type = info.GetElementType();
        if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            encodedTensor = &output;
        } else if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 || type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
            lengthTensorOut = &output;
        }
    }

    if (!encodedTensor || !lengthTensorOut) {
        throw std::runtime_error("Failed to locate RNNT encoder outputs.");
    }

    int64_t encodedLen = 0;
    {
        const auto info = lengthTensorOut->GetTensorTypeAndShapeInfo();
        if (info.GetElementCount() < 1) {
            throw std::runtime_error("RNNT encoder length output is empty.");
        }
        if (info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
            encodedLen = lengthTensorOut->GetTensorData<int32_t>()[0];
        } else {
            encodedLen = lengthTensorOut->GetTensorData<int64_t>()[0];
        }
    }

    const auto encodedInfo = encodedTensor->GetTensorTypeAndShapeInfo();
    const auto encodedShape = encodedInfo.GetShape();
    if (encodedShape.size() != 3 || encodedShape[0] != 1) {
        throw std::runtime_error("Unexpected RNNT encoder output shape.");
    }

    const float *encodedData = encodedTensor->GetTensorData<float>();
    RnntRecognizer::EncoderOutput result;
    result.timeSteps = encodedLen;

    if (encodedShape[1] == encodedLen) {
        result.featureDim = encodedShape[2];
        result.frames.assign(encodedData, encodedData + static_cast<size_t>(encodedLen * result.featureDim));
    } else if (encodedShape[2] == encodedLen) {
        result.featureDim = encodedShape[1];
        result.frames.resize(static_cast<size_t>(encodedLen * result.featureDim));
        for (int64_t t = 0; t < encodedLen; ++t) {
            for (int64_t d = 0; d < result.featureDim; ++d) {
                result.frames[static_cast<size_t>(t * result.featureDim + d)] =
                    encodedData[static_cast<size_t>(d * encodedLen + t)];
            }
        }
    } else {
        throw std::runtime_error("Cannot infer RNNT encoder time axis from output shape.");
    }

    return result;
}

size_t decoderHiddenSize()
{
    return 320;
}

Ort::Value createTokenTensor(Ort::MemoryInfo &memoryInfo,
                             ONNXTensorElementDataType elementType,
                             int token,
                             std::vector<int64_t> &shape,
                             int32_t &tokenI32,
                             int64_t &tokenI64)
{
    shape = {1, 1};
    if (elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
        tokenI32 = token;
        return Ort::Value::CreateTensor<int32_t>(memoryInfo, &tokenI32, 1, shape.data(), shape.size());
    }

    tokenI64 = token;
    return Ort::Value::CreateTensor<int64_t>(memoryInfo, &tokenI64, 1, shape.data(), shape.size());
}

std::vector<float> prepareDecoderOutForJoint(const Ort::Value &decoderOut)
{
    const float *data = decoderOut.GetTensorData<float>();
    return std::vector<float>(data, data + 320);
}

std::vector<float> runJoint(Ort::Session &joint,
                            const RnntRecognizer::TensorNameCache &inputNames,
                            const RnntRecognizer::TensorNameCache &outputNames,
                            const float *encoderFrame,
                            int64_t encoderDim,
                            const std::vector<float> &decoderForJoint)
{
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<int64_t> encShape = {1, encoderDim, 1};
    std::vector<int64_t> decShape = {1, static_cast<int64_t>(decoderForJoint.size()), 1};

    std::vector<Ort::Value> inputs;
    inputs.emplace_back(Ort::Value::CreateTensor<float>(
        memoryInfo, const_cast<float *>(encoderFrame), static_cast<size_t>(encoderDim), encShape.data(), encShape.size()));
    inputs.emplace_back(Ort::Value::CreateTensor<float>(
        memoryInfo, const_cast<float *>(decoderForJoint.data()), decoderForJoint.size(), decShape.data(), decShape.size()));

    auto outputs = joint.Run(Ort::RunOptions{nullptr},
                             inputNames.ptrs.data(),
                             inputs.data(),
                             inputs.size(),
                             outputNames.ptrs.data(),
                             outputNames.ptrs.size());

    if (outputs.empty()) {
        throw std::runtime_error("RNNT joint returned no outputs.");
    }

    return copyTensorToVector(outputs[0]);
}

std::vector<int> decodeRnnt(Ort::Session &decoder,
                            Ort::Session &joint,
                            const RnntRecognizer::TensorNameCache &decoderInputNames,
                            const RnntRecognizer::TensorNameCache &decoderOutputNames,
                            const RnntRecognizer::TensorNameCache &jointInputNames,
                            const RnntRecognizer::TensorNameCache &jointOutputNames,
                            const RnntRecognizer::EncoderOutput &encoderOut,
                            int blankId)
{
    const size_t hiddenSize = decoderHiddenSize();
    RnntRecognizer::DecoderState state{
        std::vector<float>(hiddenSize, 0.0f),
        std::vector<float>(hiddenSize, 0.0f),
    };

    const auto decoderInputType = decoder.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetElementType();
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<int> tokens;
    int64_t t = 0;
    int emittedTokens = 0;

    while (t < encoderOut.timeSteps) {
        std::vector<int64_t> tokenShape;
        int32_t tokenI32 = 0;
        int64_t tokenI64 = 0;
        const int prevToken = tokens.empty() ? blankId : tokens.back();
        Ort::Value tokenTensor = createTokenTensor(memoryInfo, decoderInputType, prevToken, tokenShape, tokenI32, tokenI64);

        std::vector<int64_t> stateShape = {1, 1, static_cast<int64_t>(hiddenSize)};
        Ort::Value hTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, state.h.data(), state.h.size(), stateShape.data(), stateShape.size());
        Ort::Value cTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, state.c.data(), state.c.size(), stateShape.data(), stateShape.size());

        std::vector<Ort::Value> decoderInputs;
        decoderInputs.emplace_back(std::move(tokenTensor));
        decoderInputs.emplace_back(std::move(hTensor));
        decoderInputs.emplace_back(std::move(cTensor));

        auto decoderOutputs = decoder.Run(Ort::RunOptions{nullptr},
                                          decoderInputNames.ptrs.data(),
                                          decoderInputs.data(),
                                          decoderInputs.size(),
                                          decoderOutputNames.ptrs.data(),
                                          decoderOutputNames.ptrs.size());

        if (decoderOutputs.size() < 3) {
            throw std::runtime_error("RNNT decoder must return dec, h, c.");
        }

        const std::vector<float> decoderForJoint = prepareDecoderOutForJoint(decoderOutputs[0]);
        std::vector<float> nextH = copyTensorToVector(decoderOutputs[1]);
        std::vector<float> nextC = copyTensorToVector(decoderOutputs[2]);

        const float *encoderFrame = encoderOut.frames.data() + static_cast<size_t>(t * encoderOut.featureDim);
        const std::vector<float> logits = runJoint(joint,
                                                   jointInputNames,
                                                   jointOutputNames,
                                                   encoderFrame,
                                                   encoderOut.featureDim,
                                                   decoderForJoint);

        const int token = static_cast<int>(std::distance(logits.begin(), std::max_element(logits.begin(), logits.end())));
        if (token != blankId) {
            tokens.push_back(token);
            state.h = std::move(nextH);
            state.c = std::move(nextC);
            ++emittedTokens;

            if (emittedTokens >= kMaxTokensPerStep) {
                ++t;
                emittedTokens = 0;
            }
        } else {
            ++t;
            emittedTokens = 0;
        }
    }

    return tokens;
}

} // namespace

RnntRecognizer::RnntRecognizer(const ModelLayout &modelLayout)
    : m_idToToken(loadTokens(modelLayout.vocab))
    , m_blankId(findBlankId(m_idToToken))
    , m_env(ORT_LOGGING_LEVEL_WARNING, "giga_asr_demo")
{
    m_sessionOptions.SetIntraOpNumThreads(1);
    m_sessionOptions.SetInterOpNumThreads(1);

    const std::vector<std::string> paths = {
        modelLayout.encoder,
        modelLayout.decoder,
        modelLayout.joint,
        modelLayout.vocab
    };
    for (size_t i = 0; i < paths.size(); ++i) {
        if (!std::filesystem::exists(paths[i])) {
            throw std::runtime_error("File not found: " + paths[i]);
        }
    }

    m_encoder.reset(new Ort::Session(m_env, modelLayout.encoder.c_str(), m_sessionOptions));
    m_decoder.reset(new Ort::Session(m_env, modelLayout.decoder.c_str(), m_sessionOptions));
    m_joint.reset(new Ort::Session(m_env, modelLayout.joint.c_str(), m_sessionOptions));

    m_encoderInputs = collectNames(*m_encoder, true);
    m_encoderOutputs = collectNames(*m_encoder, false);
    m_decoderInputs = collectNames(*m_decoder, true);
    m_decoderOutputs = collectNames(*m_decoder, false);
    m_jointInputs = collectNames(*m_joint, true);
    m_jointOutputs = collectNames(*m_joint, false);
}

std::string RnntRecognizer::Recognize(const AudioBuffer &audio) const
{
    const std::vector<float> audio16k = ResampleLinear(audio.samples, audio.sampleRate, kTargetSampleRate);
    const std::vector<float> features = computeFbank(audio16k);
    const int64_t numFrames = static_cast<int64_t>(features.size()) / kFeatureBins;
    const std::vector<float> featuresBct = transposeFeaturesToBct(features, numFrames);

    const EncoderOutput encoderOut = runEncoder(*m_encoder, m_encoderInputs, m_encoderOutputs, featuresBct, numFrames);
    const std::vector<int> tokenIds = decodeRnnt(*m_decoder,
                                                 *m_joint,
                                                 m_decoderInputs,
                                                 m_decoderOutputs,
                                                 m_jointInputs,
                                                 m_jointOutputs,
                                                 encoderOut,
                                                 m_blankId);
    return decodeRnntTokens(tokenIds, m_idToToken);
}
