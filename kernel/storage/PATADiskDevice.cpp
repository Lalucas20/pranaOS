/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <base/StringView.h>
#include <kernel/filesystem/FileDescription.h>
#include <kernel/Sections.h>
#include <kernel/storage/IDEChannel.h>
#include <kernel/storage/IDEController.h>
#include <kernel/storage/PATADiskDevice.h>

namespace Kernel {

UNMAP_AFTER_INIT NonnullRefPtr<PATADiskDevice> PATADiskDevice::create(const IDEController& controller, IDEChannel& channel, DriveType type, InterfaceType interface_type, u16 capabilities, u64 max_addressable_block)
{
    return adopt_ref(*new PATADiskDevice(controller, channel, type, interface_type, capabilities, max_addressable_block));
}

UNMAP_AFTER_INIT PATADiskDevice::PATADiskDevice(const IDEController& controller, IDEChannel& channel, DriveType type, InterfaceType interface_type, u16 capabilities, u64 max_addressable_block)
    : StorageDevice(controller, 512, max_addressable_block)
    , m_capabilities(capabilities)
    , m_channel(channel)
    , m_drive_type(type)
    , m_interface_type(interface_type)
{
}

UNMAP_AFTER_INIT PATADiskDevice::~PATADiskDevice()
{
}

StringView PATADiskDevice::class_name() const
{
    return "PATADiskDevice";
}

void PATADiskDevice::start_request(AsyncBlockDeviceRequest& request)
{
    m_channel->start_request(request, is_slave(), m_capabilities);
}

String PATADiskDevice::device_name() const
{
    return String::formatted("hd{:c}", 'a' + minor());
}

bool PATADiskDevice::is_slave() const
{
    return m_drive_type == DriveType::Slave;
}

}