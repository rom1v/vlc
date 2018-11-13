/*****************************************************************************
 * modules/misc/medialibrary/sd/device.cpp
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

#include "device.h"

namespace vlc {
  namespace medialibrary {

SDDevice::SDDevice(const std::string &mrl)
    : m_uuid(mrl)
    , m_mountpoint(mrl)
{
    // Ensure the mountpoint always ends with a '/' to avoid mismatch between
    // smb://foo and smb://foo/
    if ( *m_mountpoint.crbegin() != '/' )
    {
        m_mountpoint += '/';
        m_uuid = m_mountpoint;
    }
}

const std::string &
SDDevice::uuid() const
{
    return m_uuid;
}

bool
SDDevice::isRemovable() const
{
    return true;
}

bool
SDDevice::isPresent() const
{
    return m_present;
}

void
SDDevice::setPresent(bool present)
{
    m_present = present;
}

const
std::string &SDDevice::mountpoint() const
{
    return m_mountpoint;
}

  } /* namespace medialibrary */
} /* namespace vlc */
