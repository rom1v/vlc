/*****************************************************************************
 * mlgenre.cpp : Medialibrary's genre
 ****************************************************************************
 * Copyright (C) 2006-2018 VideoLAN and AUTHORS
 *
 * Authors: Maël Kervella <dev@maelkervella.eu>
 *          Pierre Lamot <pierre@videolabs.io>
 *          Hugo Beauzée-Luyssen <hugo@beauzee.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include <cassert>
#include "mlgenre.hpp"

MLGenre::MLGenre(const vlc_ml_genre_t *_data, QObject *_parent )
    : QObject(_parent)
    , m_id      ( _data->i_id, VLC_ML_PARENT_GENRE )
    , m_name    ( QString::fromUtf8( _data->psz_name ) )

{
    assert(_data);
}

MLGenre::MLGenre(const MLGenre &genre, QObject *_parent)
    : QObject(_parent)
    , m_id      ( genre.m_id )
    , m_name    ( genre.m_name )
{

}

MLParentId MLGenre::getId() const
{
    return m_id;
}

QString MLGenre::getName() const
{
    return m_name;
}

unsigned int MLGenre::getNbTracks() const
{
    return m_nbTracks;
}

MLGenre *MLGenre::clone(QObject *parent) const
{
    return new MLGenre(*this, parent);
}

