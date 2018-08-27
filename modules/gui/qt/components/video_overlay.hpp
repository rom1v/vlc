/*****************************************************************************
 * video_overlay.hpp : Video displayed over the medialibrary for preview
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

#ifndef VIDEOOVERLAY_HPP
#define VIDEOOVERLAY_HPP

#include <QtWidgets/QWidget>

#include "components/interface_widgets.hpp"


class VideoOverlay : public QWidget
{
    Q_OBJECT
public:
    VideoOverlay(QWidget *parent = nullptr);

    VideoWidget* getVideo();
    void setVideo( VideoWidget* v );
    void updatePosition();

private:
    QHBoxLayout *main_layout;
    VideoWidget *video;

public slots:
    void resizeToFitVideo();

};

#endif // VIDEOOVERLAY_HPP
