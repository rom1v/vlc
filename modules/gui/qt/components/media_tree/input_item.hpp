#ifndef VLC_QT_INPUT_ITEM_HPP_
#define VLC_QT_INPUT_ITEM_HPP_

#include <vlc_common.h>
#include <vlc_input_item.h>

class InputItem
{
public:
    InputItem( input_item_t *ptr = nullptr ) : ptr( ptr )
    {
        if( ptr )
            input_item_Hold( ptr );
    }

    InputItem( const InputItem &other ) : InputItem( other.ptr ) {}
    InputItem( InputItem &&other ) : ptr( std::exchange( other.ptr, nullptr ) ) {}

    ~InputItem()
    {
        if( ptr )
            input_item_Release( ptr );
    }

    InputItem &operator=( const InputItem &other )
    {
        if( other.ptr)
            input_item_Hold( other.ptr ); // hold before in case ptr == other.ptr
        if( ptr )
            input_item_Release( ptr );
        ptr = other.ptr;
        return *this;
    }

    InputItem &operator=( InputItem &&other )
    {
        ptr = std::exchange( other.ptr, nullptr );
        return *this;
    }

    bool operator==( const InputItem &other )
    {
        return ptr == other.ptr;
    }

    bool operator!=( const InputItem &other )
    {
        return !(*this == other);
    }

    input_item_t &operator*() { return *ptr; }
    const input_item_t &operator*() const { return *ptr; }

    input_item_t *operator->() { return ptr; }
    const input_item_t *operator->() const { return ptr; }

private:
    input_item_t *ptr = nullptr;
};

#endif
