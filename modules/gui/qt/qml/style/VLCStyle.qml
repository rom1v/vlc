pragma Singleton
import QtQuick 2.7

Item {
    id: vlc_style
    SystemPalette { id: activePalette; colorGroup: SystemPalette.Active }
    SystemPalette { id: inactivePalette; colorGroup: SystemPalette.Inactive }


    TextMetrics { id: fontMetrics_xxsmall; font.pixelSize: 6;  text: "lq"}
    TextMetrics { id: fontMetrics_xsmall;  font.pixelSize: 8;  text: "lq"}
    TextMetrics { id: fontMetrics_small;   font.pixelSize: 10; text: "lq"}
    TextMetrics { id: fontMetrics_normal;  font.pixelSize: 12; text: "lq"}
    TextMetrics { id: fontMetrics_large;   font.pixelSize: 14; text: "lq"}
    TextMetrics { id: fontMetrics_xlarge;  font.pixelSize: 16; text: "lq"}
    TextMetrics { id: fontMetrics_xxlarge;  font.pixelSize: 20; text: "lq"}
    TextMetrics { id: fontMetrics_xxxlarge;  font.pixelSize: 30; text: "lq"}


    property bool nightMode: false;
    function toggleNightMode() {
        if (state == "day")
            state = "night"
        else if (state == "night")
            state = "system"
        else
            state = "day"
    }


    function blendColors( a, b, blend ) {
        return Qt.rgba( a.r * blend + b.r * (1. - blend),
                        a.g * blend + b.g * (1. - blend),
                        a.b * blend + b.b * (1. - blend),
                        a.a * blend + b.a * (1. - blend))
    }

    function getBgColor(selected, hovered, focus)
    {
        if ( selected )
        {
            if (focus) return hoverBgColor
            return hoverBgColorInactive
        }
        else if (hovered)
        {
            return hoverBgColorInactive
        }
        else
        {
            if (focus) return bgColor
            return bgColorInactive
        }
    }

    state: "system"
    states: [
        State {
            name: "day"
            PropertyChanges {
                target: vlc_style

                bgColor: "white";
                textColor: "black";

                buttonColor: bgColor;
                buttonTextColor: textColor;
                buttonBorderColor: blendColors(buttonColor, buttonTextColor, 0.8)

                bgColor_removeFromPlaylist: "#CC0000";
                textColor_removeFromPlaylist: "white";

                hoverBgColor: "#F0F0F0";
                textColor_activeSource: "#FF0000";
                bannerColor: "#e6e6e6";
                hoverBannerColor: "#d6d6d6";
            }
        },
        State {
            name: "night"
            PropertyChanges {
                target: vlc_style

                bgColor: "black";
                textColor: "white";

                buttonColor: bgColor;
                buttonTextColor: textColor;
                buttonBorderColor: blendColors(buttonColor, buttonTextColor, 0.8)

                bgColor_removeFromPlaylist: "#CC0000";
                textColor_removeFromPlaylist: "white";

                hoverBgColor: "#F0F0F0";
                textColor_activeSource: "#FF0000";
                bannerColor: "#191919";
                hoverBannerColor: "#292929";
            }
        },
        State {
            name: "system"
            PropertyChanges {
                target: vlc_style

                bgColor: activePalette.base;
                bgColorInactive: inactivePalette.base;
                bgColorAlt: activePalette.alternateBase;
                bgColorAltInactive: inactivePalette.alternateBase;
                textColor: activePalette.text;

                buttonColor: activePalette.button;
                buttonTextColor: activePalette.buttonText;
                buttonBorderColor: blendColors(buttonColor, buttonTextColor, 0.8)

                bgColor_removeFromPlaylist: "#CC0000";
                textColor_removeFromPlaylist: "#FFFFFF";

                hoverBgColor: activePalette.highlight;
                hoverBgColorInactive: inactivePalette.highlight;

                textColor_activeSource: vlc_orange;
                bannerColor: activePalette.window;
                hoverBannerColor: activePalette.highlight;
            }
        }
    ]

    // Sizes
    property double margin_xxxsmall: 2;
    property double margin_xxsmall: 4;
    property double margin_xsmall: 8;
    property double margin_small: 12;
    property double margin_normal: 16;
    property double margin_large: 24;
    property double margin_xlarge: 32;

    property int fontSize_xsmall: fontMetrics_xxsmall.font.pixelSize
    property int fontSize_small:  fontMetrics_small.font.pixelSize
    property int fontSize_normal: fontMetrics_normal.font.pixelSize
    property int fontSize_large:  fontMetrics_large.font.pixelSize
    property int fontSize_xlarge: fontMetrics_xlarge.font.pixelSize
    property int fontSize_xxlarge: fontMetrics_xxlarge.font.pixelSize
    property int fontSize_xxxlarge: fontMetrics_xxxlarge.font.pixelSize

    property int fontHeight_xsmall: Math.ceil(fontMetrics_xxsmall.height)
    property int fontHeight_small:  Math.ceil(fontMetrics_small.height)
    property int fontHeight_normal: Math.ceil(fontMetrics_normal.height)
    property int fontHeight_large:  Math.ceil(fontMetrics_large.height)
    property int fontHeight_xlarge: Math.ceil(fontMetrics_xlarge.height)
    property int fontHeight_xxlarge: Math.ceil(fontMetrics_xxlarge.height)
    property int fontHeight_xxxlarge: Math.ceil(fontMetrics_xxxlarge.height)


    property int heightAlbumCover_xsmall: 32;
    property int heightAlbumCover_small: 64;
    property int heightAlbumCover_normal: 128;
    property int heightAlbumCover_large: 255;
    property int heightAlbumCover_xlarge: 512;

    property int icon_xsmall: 8;
    property int icon_small: 16;
    property int icon_normal: 32;
    property int icon_large: 64;
    property int icon_xlarge: 128;

    property int cover_xsmall: 64;
    property int cover_small: 96;
    property int cover_normal: 128;
    property int cover_large: 160;
    property int cover_xlarge: 192;

    property int heightBar_xsmall: 8;
    property int heightBar_small: 16;
    property int heightBar_normal: 32;
    property int heightBar_large: 64;
    property int heightBar_xlarge: 128;
    property int heightBar_xxlarge: 256;

    property int minWidthMediacenter: 500;
    property int maxWidthPlaylist: 400;
    property int defaultWidthPlaylist: 300;
    property int closedWidthPlaylist: 20;

    property int widthSearchInput: 200;
    property int widthSortBox: 150;

    //colors
    property color bgColor: activePalette.base;
    property color textColor: activePalette.text;
    property color bgColorInactive: inactivePalette.base;
    property color bgColorAlt: activePalette.alternateBase;
    property color bgColorAltInactive: inactivePalette.alternateBase;

    property color buttonColor: activePalette.button;
    property color buttonTextColor: activePalette.buttonText;
    property color buttonBorderColor: blendColors(activePalette.button, activePalette.buttonText, 0.8);

    property color bgColor_removeFromPlaylist: "#CC0000";
    property color textColor_removeFromPlaylist: "white";

    property color hoverBgColor: activePalette.highlight;
    property color hoverBgColorInactive: inactivePalette.highlight;
    property color textColor_activeSource: "#FF0000";

    property color bannerColor: activePalette.window;
    property color hoverBannerColor: activePalette.highlight;

    property color vlc_orange: "#FFFF950D";

    property color alertColor: "red";


    //timings
    property int delayToolTipAppear: 500;
    property int timingPlaylistClose: 1000;
    property int timingPlaylistOpen: 1000;
    property int timingGridExpandOpen: 200;
    property int timingListExpandOpen: 200;

    //default arts
    property url noArtCover: "qrc:///noart.png";


}
