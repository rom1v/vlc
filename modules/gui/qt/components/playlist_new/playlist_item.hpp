#ifndef VLC_QT_PLAYLIST_ITEM_HPP_
#define VLC_QT_PLAYLIST_ITEM_HPP_

#include <vlc_input_item.h>
#include <vlc_playlist_new.h>
#include <QSharedData>

class PlaylistItemPtr
{
public:
    PlaylistItemPtr(vlc_playlist_item_t *ptr = nullptr)
        : ptr(ptr)
    {
        if (ptr)
            vlc_playlist_item_Hold(ptr);
    }

    PlaylistItemPtr(const PlaylistItemPtr &other)
        : PlaylistItemPtr(other.ptr) {}

    PlaylistItemPtr(PlaylistItemPtr &&other) noexcept
        : ptr(std::exchange(other.ptr, nullptr)) {}

    ~PlaylistItemPtr()
    {
        if (ptr)
            vlc_playlist_item_Release(ptr);
    }

    PlaylistItemPtr &operator=(const PlaylistItemPtr &other)
    {
        if (other.ptr)
            /* hold before in case ptr == other.ptr */
            vlc_playlist_item_Hold(other.ptr);
        if (ptr)
            vlc_playlist_item_Release(ptr);
        ptr = other.ptr;
        return *this;
    }

    PlaylistItemPtr &operator=(PlaylistItemPtr &&other) noexcept
    {
        ptr = std::exchange(other.ptr, nullptr);
        return *this;
    }

    bool operator==(const PlaylistItemPtr &other)
    {
        return ptr == other.ptr;
    }

    bool operator!=(const PlaylistItemPtr &other)
    {
        return !(*this == other);
    }

    operator bool() const
    {
        return ptr;
    }

    vlc_playlist_item_t *data() const
    {
        return ptr;
    }

    input_item_t *getMedia() const
    {
        return vlc_playlist_item_GetMedia(ptr);
    }

private:
    /* vlc_playlist_item_t is opaque, no need for * and -> */
    vlc_playlist_item_t *ptr = nullptr;
};

/* PlaylistItemPtr + with cached data */
class PlaylistItem
{
public:
    PlaylistItem(vlc_playlist_item_t *item = nullptr)
    {
        if (item)
        {
            d = new Data();
            d->item = item;
            sync();
        }
    }

    bool operator==(const PlaylistItem &other)
    {
        return d == other.d || (d && other.d && d->item == other.d->item);
    }

    bool operator!=(const PlaylistItem &other)
    {
        return !(*this == other);
    }

    operator bool() const
    {
        return d;
    }

    vlc_playlist_item_t *data() const {
        return d ? d->item.data() : nullptr;
    }

    QString getTitle() const
    {
        return d->title;
    }

    void sync() {
        input_item_t *media = d->item.getMedia();
        vlc_mutex_lock(&media->lock);
        d->title = media->psz_name;
        vlc_mutex_unlock(&media->lock);
    }

private:
    struct Data : public QSharedData {
        PlaylistItemPtr item;

        /* cached values */
        QString title;
    };

    QSharedDataPointer<Data> d;
};

/* PlaylistItem has the same size as a raw pointer */
static_assert(sizeof(PlaylistItem) == sizeof(void *));

#endif
