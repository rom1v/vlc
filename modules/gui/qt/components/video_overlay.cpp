/*****************************************************************************
 * video_overlay.cpp : Video displayed over the medialibrary for preview
 ****************************************************************************
 * Copyright (C) 2006-2011 VideoLAN and AUTHORS
 * $Id$
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

#include "video_overlay.hpp"

VideoOverlay::VideoOverlay(QWidget *parent) : QWidget(parent),
    main_layout( new QHBoxLayout() ),
    video( nullptr )
{
    main_layout->setMargin(0);
    setLayout(main_layout);
    resizeToFitVideo();
    updatePosition();

}

VideoWidget* VideoOverlay::getVideo() {
    return video;
}

void VideoOverlay::setVideo( VideoWidget* v ) {
    video = v;
    if (video)
        main_layout->addWidget(video);
    resizeToFitVideo();
    updatePosition();
    CONNECT(video, sizeChanged(int, int), this, resizeToFitVideo());
}

void VideoOverlay::resizeToFitVideo() {
    if (video)
    {
        QSize max_size(300, 200);
        if ( video->width() / max_size.width() < video->height() / max_size.height() )
            resize( max_size.height() * video->width() / video->height(), max_size.height() );
        else
            resize( max_size.width(), max_size.width() * video->height() / video->width() );
    }
    else
        resize(0,0);
}

void VideoOverlay::updatePosition() {
    if (video)
        move(20, parentWidget()->height() - this->height() - 20);
    else
        move(0,0);
}

