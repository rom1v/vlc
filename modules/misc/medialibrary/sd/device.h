/*****************************************************************************
 * modules/misc/medialibrary/sd/device.h
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

#ifndef SD_DEVICE_H
#define SD_DEVICE_H

#include <medialibrary/filesystem/IDevice.h>

namespace vlc {
  namespace medialibrary {

using namespace ::medialibrary::fs;

class SDDevice : public IDevice
{
public:
    SDDevice(const std::string &mrl);

    const std::string &uuid() const override;
    bool isRemovable() const override;
    bool isPresent() const override;
    void setPresent(bool present) override;
    const std::string &mountpoint() const override;

private:
    std::string m_uuid;
    std::string m_mountpoint;
    bool m_present = true;
};

  } /* namespace medialibrary */
} /* namespace vlc */

#endif
