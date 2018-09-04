import QtQuick 2.7

Item {
    id: colors_id

    SystemPalette { id: activePalette; colorGroup: SystemPalette.Active }
    SystemPalette { id: inactivePalette; colorGroup: SystemPalette.Inactive }

    function changeColorTheme() {
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

    function setColorAlpha( c, alpha )
    {
        return Qt.rgba(c.r, c.g, c.b, alpha)
    }

    function getBgColor(selected, hovered, focus)
    {
        if ( selected )
        {
            if (focus) return bgHover
            return bgHoverInactive
        }
        else if (hovered)
        {
            return bgHoverInactive
        }
        else
        {
            if (focus) return bg
            return bgInactive
        }
    }

    property color text: activePalette.text;

    property color bg: activePalette.base;
    property color bgInactive: inactivePalette.base;

    //for alternate rows
    property color bgAlt: activePalette.alternateBase;
    property color bgAltInactive: inactivePalette.alternateBase;

    property color bgHover: activePalette.highlight;
    property color bgHoverInactive: inactivePalette.highlight;

    property color button: activePalette.button;
    property color buttonText: activePalette.buttonText;
    property color buttonBorder: blendColors(activePalette.button, activePalette.buttonText, 0.8);

    property color textActiveSource: "red";

    property color banner: activePalette.window;
    property color bannerHover: activePalette.highlight;

    //vlc orange
    property color accent: "#FFFF950D";

    property color alert: "red";

    state: "system"
    states: [
        //other "ugly" styles are provided for testing purpose
        State {
            name: "day"
            PropertyChanges {
                target: colors_id

                text: "black"

                bg: "white"
                bgInactive: "white"

                bgAlt: "lightgrey"
                bgAltInactive: "lightgrey"

                bgHover: "green"
                bgHoverInactive: "lightgreen"

                button: "ivory";
                buttonText: "black";
                buttonBorder: blendColors(activePalette.button, activePalette.buttonText, 0.8);

                textActiveSource: "red";

                banner: "beige";
                bannerHover: "green";

                accent: "blue";
                alert: "red";
            }
        },
        State {
            name: "night"
            PropertyChanges {
                target: colors_id

                text: "white"

                bg: "black"
                bgInactive: "black"

                bgAlt: "darkgrey"
                bgAltInactive: "darkgrey"

                bgHover: "red"
                bgHoverInactive: "darkred"

                button: "#111111"
                buttonText: "white"
                buttonBorder: blendColors(activePalette.button, activePalette.buttonText, 0.8)

                textActiveSource: "green"

                banner: "#222222"
                bannerHover: "green"

                accent: "yellow"
                alert: "red"
            }
        },
        State {
            name: "system"
            PropertyChanges {
                target: colors_id

                bg: activePalette.base
                bgInactive: inactivePalette.base

                bgAlt: activePalette.alternateBase
                bgAltInactive: inactivePalette.alternateBase

                bgHover: activePalette.highlight
                bgHoverInactive: inactivePalette.highlight

                text: activePalette.text

                button: activePalette.button
                buttonText: activePalette.buttonText
                buttonBorder: blendColors(button, buttonText, 0.8)

                textActiveSource: accent
                banner: activePalette.window
                bannerHover: activePalette.highlight
            }
        }
    ]
}
