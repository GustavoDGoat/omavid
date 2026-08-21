import QtQuick
import QtQuick.Controls
import "Format.js" as Format

// The merge audio track: a timeline spanning the whole merged video, where each
// audio clip is a draggable block. Drag the body to reposition, drag an edge to
// trim, and × to remove.
Item {
    id: root
    implicitHeight: 56

    property var model: null
    property real totalDuration: 0
    property color accent: "#FFD60A"

    readonly property real padX: 8
    readonly property real trackW: width - 2 * padX
    readonly property real blockH: 34

    function xForTime(t) {
        if (totalDuration <= 0)
            return padX;
        return padX + (t / totalDuration) * trackW;
    }
    function timeForX(x) {
        if (trackW <= 0 || totalDuration <= 0)
            return 0;
        return Math.max(0, Math.min(1, (x - padX) / trackW)) * totalDuration;
    }

    Rectangle {
        x: root.padX
        y: 0
        width: root.trackW
        height: root.height
        radius: 6
        color: "#161618"
    }

    Repeater {
        model: root.model
        delegate: Rectangle {
            id: block
            x: root.xForTime(model.positionSec)
            y: (root.height - root.blockH) / 2
            width: Math.max(20, (model.endSec - model.startSec) / Math.max(root.totalDuration, 0.001) * root.trackW)
            height: root.blockH
            radius: 6
            color: "#3a3a3e"
            border.color: root.accent
            border.width: 1

            HoverHandler { id: blockHover }

            Label {
                anchors.centerIn: parent
                width: parent.width - 22
                text: model.name
                color: "white"
                font.pixelSize: 10
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 3
                anchors.top: parent.top
                anchors.topMargin: 3
                width: 14
                height: 14
                radius: 7
                color: "#2c2c2f"
                visible: blockHover.hovered
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: "white"
                    font.pixelSize: 11
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.removeAudio(index)
                }
            }

            // body drag: reposition the clip on the timeline
            MouseArea {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                cursorShape: Qt.OpenHandCursor
                property real pressX: 0
                property real startPosition: 0
                onPressed: function (mouse) {
                    pressX = mouse.x;
                    startPosition = model.positionSec;
                }
                onPositionChanged: function (mouse) {
                    var dx = (mouse.x - pressX) / Math.max(root.trackW, 0.001) * root.totalDuration;
                    backend.setAudioPosition(index, startPosition + dx);
                }
            }

            // left edge: trim start
            MouseArea {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 10
                cursorShape: Qt.SizeHorCursor
                property real pressX: 0
                property real startStart: 0
                property real startEnd: 0
                onPressed: function (mouse) {
                    pressX = mouse.x;
                    startStart = model.startSec;
                    startEnd = model.endSec;
                }
                onPositionChanged: function (mouse) {
                    var dx = (mouse.x - pressX) / Math.max(root.trackW, 0.001) * root.totalDuration;
                    var minGap = Math.min(0.1, model.duration);
                    backend.setAudioTrim(index, Math.max(0, Math.min(startStart + dx, startEnd - minGap)), startEnd);
                }
            }

            // right edge: trim end
            MouseArea {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 10
                cursorShape: Qt.SizeHorCursor
                property real pressX: 0
                property real startStart: 0
                property real startEnd: 0
                onPressed: function (mouse) {
                    pressX = mouse.x;
                    startStart = model.startSec;
                    startEnd = model.endSec;
                }
                onPositionChanged: function (mouse) {
                    var dx = (mouse.x - pressX) / Math.max(root.trackW, 0.001) * root.totalDuration;
                    var minGap = Math.min(0.1, model.duration);
                    backend.setAudioTrim(index, startStart,
                                         Math.min(model.duration, Math.max(startStart + minGap, startEnd + dx)));
                }
            }
        }
    }

    Label {
        x: root.padX
        y: root.height + 2
        text: "0:00"
        color: "#9a9aa0"
        font.pixelSize: 10
        font.family: "monospace"
    }
    Label {
        anchors.right: parent.right
        anchors.rightMargin: root.padX
        y: root.height + 2
        text: Format.fmt(root.totalDuration)
        color: "#9a9aa0"
        font.pixelSize: 10
        font.family: "monospace"
    }
}
