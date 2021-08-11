/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <kernel/bus/pci/IDs.h>
#include <kernel/graphics/console/GenericFramebufferConsole.h>
#include <kernel/graphics/GraphicsManagement.h>
#include <kernel/graphics/virtiogpu/GPU.h>
#include <kernel/graphics/virtiogpu/GraphicsAdapter.h>

namespace Kernel::Graphics::VirtIOGPU {

NonnullRefPtr<GraphicsAdapter> GraphicsAdapter::initialize(PCI::Address base_address)
{
    VERIFY(PCI::get_id(base_address).vendor_id == PCI::VendorID::VirtIO);
    return adopt_ref(*new GraphicsAdapter(base_address));
}

GraphicsAdapter::GraphicsAdapter(PCI::Address base_address)
    : PCI::DeviceController(base_address)
{
    m_gpu_device = adopt_ref(*new GPU(base_address)).leak_ref();
}

void GraphicsAdapter::initialize_framebuffer_devices()
{
    dbgln_if(VIRTIO_DEBUG, "GPU: Initializing framebuffer devices");
    VERIFY(!m_created_framebuffer_devices);
    m_gpu_device->create_framebuffer_devices();
    m_created_framebuffer_devices = true;

    GraphicsManagement::the().m_console = m_gpu_device->default_console();
}

void GraphicsAdapter::enable_consoles()
{
    dbgln_if(VIRTIO_DEBUG, "GPU: Enabling consoles");
    m_gpu_device->for_each_framebuffer([&](auto& framebuffer, auto& console) {
        framebuffer.deactivate_writes();
        console.enable();
        return IterationDecision::Continue;
    });
}

void GraphicsAdapter::disable_consoles()
{
    dbgln_if(VIRTIO_DEBUG, "GPU: Disabling consoles");
    m_gpu_device->for_each_framebuffer([&](auto& framebuffer, auto& console) {
        console.disable();
        framebuffer.activate_writes();
        return IterationDecision::Continue;
    });
}

}
