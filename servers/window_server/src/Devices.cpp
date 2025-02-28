/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Devices.h"
#include <fcntl.h>
#include <libfoundation/Logger.h>

namespace WinServer {

Devices* s_WinServer_Devices_the = nullptr;

Devices::Devices()
{
    s_WinServer_Devices_the = this;
    m_mouse_fd = open("/dev/mouse", O_RDONLY);
    if (m_mouse_fd < 0) {
        Logger::debug << "Can't open mouse" << std::endl;
        std::abort();
    }

    m_keyboard_fd = open("/dev/kbd", O_RDONLY);
    if (m_keyboard_fd < 0) {
        Logger::debug << "Can't open keyboard" << std::endl;
        std::abort();
    }

    LFoundation::EventLoop::the().add(
        m_mouse_fd, [] {
            Devices::the().pump_mouse();
        },
        nullptr);

    LFoundation::EventLoop::the().add(
        m_keyboard_fd, [] {
            Devices::the().pump_keyboard();
        },
        nullptr);
}

} // namespace WinServer