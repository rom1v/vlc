import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.1
import QtQml.Models 2.2
import org.videolan.medialib 0.1


import "qrc:///utils/" as Utils
import "qrc:///style/"

//This component is designed to be used by MusicAlbumsDisplay
Item {
    width: VLCStyle.cover_normal
    height: VLCStyle.cover_normal + VLCStyle.fontHeight_normal + VLCStyle.margin_xsmall
    Utils.GridItem {

        //justify elements in the grid by parenting them to a placehoder item and moving them afterwards
        x: ((model.index % gridView_id._colCount) + 1) * (gridView_id.rightSpace / (gridView_id._colCount + 1))
        width: parent.width
        height: parent.height

        color: VLCStyle.colors.getBgColor(element.DelegateModel.inSelected, this.hovered, root.activeFocus)

        cover : Image {
            source: model.cover || VLCStyle.noArtCover
        }

        name : model.title || "Unknown title"
        date : model.release_year !== 0 ? model.release_year : ""
        infos : model.duration + " - " + model.nb_tracks + " tracks"

        onItemClicked : root._gridItemClicked(keys, modifier, model.index)
        onPlayClicked: medialib.addAndPlay( model.id )
        onAddToPlaylistClicked : medialib.addToPlaylist( model.id );
    }
}
