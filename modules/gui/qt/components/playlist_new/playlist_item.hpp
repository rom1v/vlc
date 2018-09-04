#ifndef VLC_QT_PLAYLIST_ITEM_HPP_
#define VLC_QT_PLAYLIST_ITEM_HPP_

#include <vlc_input_item.h>
#include <vlc_playlist_new.h>

class PlaylistItem
{
public:
    PlaylistItem(vlc_playlist_item_t *ptr = nullptr)
        : ptr(ptr)
    {
        if (ptr)
            vlc_playlist_item_Hold(ptr);
    }

    PlaylistItem(const PlaylistItem &other)
        : PlaylistItem(other.ptr) {}

    PlaylistItem(PlaylistItem &&other) noexcept
        : ptr(std::exchange(other.ptr, nullptr)) {}

    ~PlaylistItem()
    {
        if (ptr)
            vlc_playlist_item_Release(ptr);
    }

    PlaylistItem &operator=(const PlaylistItem &other)
    {
        if (other.ptr)
            /* hold before in case ptr == other.ptr */
            vlc_playlist_item_Hold(other.ptr);
        if (ptr)
            vlc_playlist_item_Release(ptr);
        ptr = other.ptr;
        return *this;
    }

    PlaylistItem &operator=(PlaylistItem &&other)
    {
        ptr = std::exchange(other.ptr, nullptr);
        return *this;
    }

    bool operator==(const PlaylistItem &other)
    {
        return ptr == other.ptr;
    }

    bool operator!=(const PlaylistItem &other)
    {
        return !(*this == other);
    }

    vlc_playlist_item_t &operator*()
    {
        return *ptr;
    }

    const vlc_playlist_item_t &operator*() const
    {
        return *ptr;
    }

    vlc_playlist_item_t *operator->()
    {
        return ptr;
    }

    const vlc_playlist_item_t *operator->() const
    {
        return ptr;
    }

    operator bool() const
    {
        return ptr;
    }

    input_item_t *getMedia() const
    {
        return vlc_playlist_item_GetMedia(ptr);
    }

private:
    vlc_playlist_item_t *ptr = nullptr;
};

#endif
