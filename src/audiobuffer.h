// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <vector>

struct AudioBuffer {
    int sampleRate = 0;
    std::vector<float> samples;
};
