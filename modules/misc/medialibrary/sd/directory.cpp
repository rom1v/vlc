/*****************************************************************************
 * modules/misc/medialibrary/sd/directory.cpp
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

#include "directory.h"
#include "file.h"

#include <assert.h>
#include <vector>
#include <system_error>
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_threads.h>
#include <vlc_cxx_helpers.hpp>

using InputItemPtr = vlc_shared_data_ptr_type(input_item_t,
                                              input_item_Hold,
                                              input_item_Release);

namespace vlc {
  namespace medialibrary {

SDDirectory::SDDirectory(const std::string &mrl, SDFileSystemFactory &fs)
    : m_mrl(mrl)
    , m_fs(fs)
{
}

const std::string &
SDDirectory::mrl() const
{
    return m_mrl;
}

const std::vector<std::shared_ptr<IFile>> &
SDDirectory::files() const
{
    if (!read_done)
        read();
    return m_files;
}

const std::vector<std::shared_ptr<IDirectory>> &
SDDirectory::dirs() const
{
    if (!read_done)
        read();
    return m_dirs;
}

std::shared_ptr<IDevice>
SDDirectory::device() const
{
    if (!m_device)
        m_device = m_fs.createDeviceFromMrl(mrl());
    return m_device;
}

struct metadata_request {
    vlc::threads::semaphore sem;
    /* results */
    enum input_item_preparse_status status;
    std::vector<InputItemPtr> *children;
};

  } /* namespace medialibrary */
} /* namespace vlc */

extern "C" {

static void
subtree_added(input_item_t *media, input_item_node_t *subtree, void *userdata)
{
    VLC_UNUSED(media);
    auto *req = static_cast<vlc::medialibrary::metadata_request *>(userdata);
    for (int i = 0; i < subtree->i_children; ++i)
    {
        input_item_node_t *child = subtree->pp_children[i];
         /* this class assumes we always receive a flat list */
        assert(child->i_children == 0);
        input_item_t *media = child->p_item;

        req->children->push_back(InputItemPtr(media));
    }
}

static void
preparse_ended(input_item_t *media, enum input_item_preparse_status status,
               void *userdata)
{
    VLC_UNUSED(media);
    auto *req = static_cast<vlc::medialibrary::metadata_request *>(userdata);
    req->status = status;
    req->sem.post();
}

static const input_preparser_callbacks_t callbacks = {
    .on_preparse_ended = preparse_ended,
    .on_subtree_added = subtree_added,
};

} /* extern C */

namespace vlc {
  namespace medialibrary {

static enum input_item_preparse_status
request_metadata_sync(libvlc_int_t *libvlc, input_item_t *media,
                      input_item_meta_request_option_t options,
                      int timeout, std::vector<InputItemPtr> *out_children)
{
    metadata_request req;
    req.children = out_children;

    int res = libvlc_MetadataRequest(libvlc, media, options, &callbacks, &req,
                                     timeout, NULL);
    if (res != VLC_SUCCESS)
        return ITEM_PREPARSE_FAILED;

    req.sem.wait();
    return req.status;
}

void
SDDirectory::read() const
{
    input_item_t *media = input_item_New(m_mrl.c_str(), m_mrl.c_str());
    if (!media)
        throw std::bad_alloc();

    std::vector<InputItemPtr> children;

    auto options = static_cast<input_item_meta_request_option_t>(
            META_REQUEST_OPTION_SCOPE_LOCAL |
            META_REQUEST_OPTION_SCOPE_NETWORK);
    vlc_tick_t timeout = VLC_TICK_FROM_SEC(5);

    enum input_item_preparse_status status =
            request_metadata_sync(m_fs.libvlc(), media, options, timeout,
                                  &children);
    assert(status != ITEM_PREPARSE_SKIPPED); /* network flag enabled */

    if (status == ITEM_PREPARSE_TIMEOUT)
        throw std::system_error(ETIMEDOUT, std::generic_category(),
                                "Failed to browse network directory: "
                                "Network is too slow");
    if (status == ITEM_PREPARSE_FAILED)
        throw std::system_error(EIO, std::generic_category(),
                                "Failed to browse network directory: "
                                "Unknown error");

    for (const InputItemPtr &media : children)
    {
        const char *mrl = media.get()->psz_uri;
        enum input_item_type_e type = media->i_type;
        if (type == ITEM_TYPE_DIRECTORY)
            m_dirs.push_back(std::make_shared<SDDirectory>(mrl, m_fs));
        else if (type == ITEM_TYPE_FILE)
            m_files.push_back(std::make_shared<SDFile>(mrl));
    }

    read_done = true;
}

  } /* namespace medialibrary */
} /* namespace vlc */
