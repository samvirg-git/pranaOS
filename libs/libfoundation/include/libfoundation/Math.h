/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>

namespace LFoundation {

[[gnu::always_inline]] inline float fast_inv_sqrt(float x)
{
    float xhalf = 0.5f * x;
    int i = *(int*)&x;
    i = 0x5f3759df - (i >> 1);
    x = *(float*)&i;
    x = x * (1.5f - xhalf * x * x);
    return x;
}

[[gnu::always_inline]] inline float fast_sqrt(float x)
{
    return 1.0 / fast_inv_sqrt(x);
}

} // namespace LFoundation