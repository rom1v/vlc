/*****************************************************************************
 * mlvideomodel.hpp: MediaLibrary's video listing model
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

#ifndef MCVIDEOMODEL_H
#define MCVIDEOMODEL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_media_library.h>

#include "mlbasemodel.hpp"
#include "mlvideo.hpp"

#include <QObject>

class MLVideoModel : public MLSlidingWindowModel<MLVideo>
{
    Q_OBJECT

public:
    explicit MLVideoModel(QObject* parent = nullptr);
    virtual ~MLVideoModel() = default;

    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<std::unique_ptr<MLVideo>> fetch() override;
    size_t countTotalElements() const override;
    vlc_ml_sorting_criteria_t roleToCriteria(int role) const override;
    vlc_ml_sorting_criteria_t nameToCriteria(QByteArray name) const override;
    virtual void onVlcMlEvent( const vlc_ml_event_t* event ) override;

    static QHash<QByteArray, vlc_ml_sorting_criteria_t> M_names_to_criteria;
};

#endif // MCVIDEOMODEL_H
