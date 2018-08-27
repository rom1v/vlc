/*****************************************************************************
 * mlserie.hpp : Medialibrary's show
 ****************************************************************************
 * Copyright (C) 2006-2018 VideoLAN and AUTHORS
 *
 * Authors: Maël Kervella <dev@maelkervella.eu>
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
#ifndef MLSERIE_HPP
#define MLSERIE_HPP

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "vlc_common.h"

#include <memory>
#include <QString>
#include <QObject>
#include <vlc_media_library.h>
#include "mlhelper.hpp"

class MLShow : public QObject
{
    Q_OBJECT

    Q_PROPERTY(uint64_t id READ getId CONSTANT)
    Q_PROPERTY(QString title READ getTitle CONSTANT)
    Q_PROPERTY(QString duration READ getDuration CONSTANT)
    Q_PROPERTY(QString cover READ getCover CONSTANT)

public:
    MLShow(const vlc_ml_show_t *_data );

    uint64_t getId() { return 0; }
    QString getTitle() { return QString(""); }
    QString getDuration() { return QString(""); }
    QString getCover() { return QString(""); }
};

#endif
