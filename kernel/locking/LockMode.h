/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

namespace Kernel {

enum class LockMode : u8 {
    Unlocked,
    Shared,
    Exclusive
};

}
