#ifndef VLC_QT_PLAYLIST_ITEM_HPP_
#define VLC_QT_PLAYLIST_ITEM_HPP_

#include <vlc_input_item.h>
#include <vlc_playlist_new.h>
#include <QSharedData>

class PlaylistItem
{
public:
    PlaylistItem(vlc_playlist_item_t *ptr = nullptr)
        : ptr(ptr)
    {
        if (ptr)
        {
            vlc_playlist_item_Hold(ptr);
            sync();
        }
    }

    PlaylistItem(const PlaylistItem &other)
        : ptr(other.ptr)
        , meta(other.meta)
    {
        if (ptr)
            vlc_playlist_item_Hold(ptr);
    }

    PlaylistItem(PlaylistItem &&other) noexcept
        : ptr(std::exchange(other.ptr, nullptr))
        , meta(std::move(other.meta)) {}

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
        meta = other.meta;
        return *this;
    }

    PlaylistItem &operator=(PlaylistItem &&other) noexcept
    {
        ptr = std::exchange(other.ptr, nullptr);
        meta = std::move(other.meta);
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

    QString getTitle() const
    {
        return meta->title;
    }

    void sync() {
        input_item_t *media = getMedia();
        vlc_mutex_lock(&media->lock);
        meta->title = media->psz_name;
        vlc_mutex_unlock(&media->lock);
    }

private:
    struct Meta : public QSharedData {
        QString title;
        /* TODO other fields */
    };

    vlc_playlist_item_t *ptr = nullptr;

    /* cached values, updated by sync() */
    QSharedDataPointer<Meta> meta;
};

#endif
