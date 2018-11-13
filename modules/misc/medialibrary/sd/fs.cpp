/*****************************************************************************
 * modules/misc/medialibrary/sd/fs.cpp
 *****************************************************************************
 * Copyright (C) 2018 VLC authors, VideoLAN and VideoLabs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "fs.h"

#include <algorithm>
#include <vlc_services_discovery.h>
#include <medialibrary/IDeviceLister.h>
#include <medialibrary/filesystem/IDevice.h>

#include "device.h"
#include "directory.h"
#include "util.h"

extern "C" {

static void
services_discovery_item_added(services_discovery_t *sd,
                              input_item_t *parent, input_item_t *media,
                              const char *cat)
{
    VLC_UNUSED(parent);
    VLC_UNUSED(cat);
    vlc::medialibrary::SDFileSystemFactory *that =
        static_cast<vlc::medialibrary::SDFileSystemFactory *>(sd->owner.sys);
    that->onDeviceAdded(media);
}

static void
services_discovery_item_removed(services_discovery_t *sd, input_item_t *media)
{
    vlc::medialibrary::SDFileSystemFactory *that =
        static_cast<vlc::medialibrary::SDFileSystemFactory *>(sd->owner.sys);
    that->onDeviceRemoved(media);
}

static const struct services_discovery_callbacks sd_cbs = {
    .item_added = services_discovery_item_added,
    .item_removed = services_discovery_item_removed,
};

}

namespace vlc {
  namespace medialibrary {

using namespace ::medialibrary;

SDFileSystemFactory::SDFileSystemFactory(vlc_object_t *parent,
                                         const std::string &name,
                                         const std::string &scheme)
    : parent(parent)
    , m_name(name)
    , m_scheme(scheme)
{
}

std::shared_ptr<IDirectory>
SDFileSystemFactory::createDirectory(const std::string &mrl)
{
    return std::make_shared<SDDirectory>(mrl, *this);
}

std::shared_ptr<IDevice>
SDFileSystemFactory::createDevice(const std::string &uuid)
{
    std::shared_ptr<IDevice> res;

    vlc::threads::mutex_locker locker(mutex);

    vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    do {
        auto it = std::find_if(devices.cbegin(), devices.cend(),
                [&uuid](const std::shared_ptr<IDevice> &device) {
                    return uuid == device->uuid();
                });
        if (it != devices.cend())
            res = *it;
        else
        {
            /* wait a bit, maybe the device is not detected yet */
            int timeout = itemAddedCond.timedwait(mutex, deadline);
            if (timeout)
                break;
        }
    } while (!res);

    return res;
}

std::shared_ptr<IDevice>
SDFileSystemFactory::createDeviceFromMrl(const std::string &mrl)
{
    /* for SD, uuid == mrl */
    return createDevice(mrl);
}

void
SDFileSystemFactory::refreshDevices()
{
    /* nothing to do */
}

bool
SDFileSystemFactory::isMrlSupported(const std::string &path) const
{
    auto s = m_scheme + ":";
    return !path.compare(0, s.length(), s);
}

bool
SDFileSystemFactory::isNetworkFileSystem() const
{
    return true;
}

const std::string &
SDFileSystemFactory::scheme() const
{
    return m_scheme;
}

void
SDFileSystemFactory::start(IFileSystemFactoryCb *callbacks)
{
    this->callbacks = callbacks;
    struct services_discovery_owner_t owner = {
        .cbs = &sd_cbs,
        .sys = this,
    };
    sd.reset(vlc_sd_Create(parent, m_name.c_str(), &owner));
    if (!sd)
        throw std::bad_alloc();
}

void
SDFileSystemFactory::stop()
{
    sd.reset();
    callbacks = nullptr;
}

libvlc_int_t *
SDFileSystemFactory::libvlc() const
{
    return parent->obj.libvlc;
}

void
SDFileSystemFactory::onDeviceAdded(input_item_t *media)
{
    auto uuid = media->psz_uri;

    {
        vlc::threads::mutex_locker locker(mutex);
        auto it = std::find_if(devices.cbegin(), devices.cend(),
                [&uuid](const std::shared_ptr<IDevice> &device) {
                    return uuid == device->uuid();
                });
        if (it != devices.cend())
            return; /* already exists */

        auto device = std::make_shared<SDDevice>(media->psz_uri);
        devices.push_back(device);
    }

    itemAddedCond.signal();
    callbacks->onDevicePlugged(uuid);
}

void
SDFileSystemFactory::onDeviceRemoved(input_item_t *media)
{
    auto uuid = media->psz_uri;

    {
        vlc::threads::mutex_locker locker(mutex);
        auto it = std::remove_if(devices.begin(), devices.end(),
                [&uuid](const std::shared_ptr<IDevice> &device) {
                    return uuid == device->uuid();
                });
        devices.erase(it, devices.end());
    }

    callbacks->onDeviceUnplugged(uuid);
}

  } /* namespace medialibrary */
} /* namespace vlc */
