import QtQuick
import QtQuick.Controls
import "Format.js" as Format

// A minimal start/end range editor for a single audio clip (no filmstrip). The
// two handles trim the audio; the selection is highlighted in the accent color.
Item {
    id: root
    implicitHeight: 48

    property real durationSec: 0
    property real startSec: 0
    property real endSec: 0
    property color accent: "#FFD60A"
    readonly property bool trimming: mouse.mode !== 0

    readonly property real handleW: 14
    readonly property real trackX: handleW
    readonly property real trackW: width - 2 * handleW

    function xForTime(t) {
        if (durationSec <= 0)
            return trackX;
        return trackX + (t / durationSec) * trackW;
    }
    function timeForX(x) {
        if (trackW <= 0 || durationSec <= 0)
            return 0;
        return Math.max(0, Math.min(1, (x - trackX) / trackW)) * durationSec;
    }

    Rectangle {
        id: track
        x: root.trackX
        y: 14
        width: root.trackW
        height: root.height - 28
        radius: 6
        color: "#1c1c1e"
        clip: true

        Rectangle {
            x: root.xForTime(root.startSec) - track.x
            width: root.xForTime(root.endSec) - root.xForTime(root.startSec)
            height: track.height
            color: root.accent
            opacity: 0.28
        }
    }

    // dim outside the selection
    Rectangle {
        x: track.x
        y: track.y
        width: Math.max(0, root.xForTime(root.startSec) - track.x)
        height: track.height
        color: "#00000099"
    }
    Rectangle {
        x: root.xForTime(root.endSec)
        y: track.y
        width: Math.max(0, track.x + track.width - root.xForTime(root.endSec))
        height: track.height
        color: "#00000099"
    }

    // selection frame + handles
    Rectangle {
        x: root.xForTime(root.startSec) - root.handleW
        y: track.y - 3
        width: (root.xForTime(root.endSec) - root.xForTime(root.startSec)) + 2 * root.handleW
        height: track.height + 6
        radius: 7
        color: "transparent"
        border.color: root.accent
        border.width: 2
    }

    Component {
        id: handleComp
        Rectangle {
            radius: 4
            color: root.accent
            Rectangle {
                anchors.centerIn: parent
                width: 2
                height: 12
                radius: 1
                color: "#1c1c1e"
            }
        }
    }
    Loader {
        sourceComponent: handleComp
        x: root.xForTime(root.startSec) - root.handleW
        y: track.y - 3
        width: root.handleW
        height: track.height + 6
    }
    Loader {
        sourceComponent: handleComp
        x: root.xForTime(root.endSec)
        y: track.y - 3
        width: root.handleW
        height: track.height + 6
    }

    Label {
        x: 0
        y: 0
        text: Format.fmt(root.startSec)
        color: "#9a9aa0"
        font.pixelSize: 10
        font.family: "monospace"
    }
    Label {
        anchors.right: parent.right
        y: 0
        text: Format.fmt(root.endSec) + " / " + Format.fmt(root.durationSec)
        color: "#9a9aa0"
        font.pixelSize: 10
        font.family: "monospace"
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        property int mode: 0

        function hitTest(x) {
            var x0 = root.xForTime(root.startSec);
            var x1 = root.xForTime(root.endSec);
            if (Math.abs(x - (x0 - root.handleW / 2)) <= root.handleW)
                return 1;
            if (Math.abs(x - (x1 + root.handleW / 2)) <= root.handleW)
                return 2;
            return 0;
        }

        cursorShape: Qt.ArrowCursor
        onPositionChanged: function (mouse) {
            if (root.durationSec <= 0)
                return;
            if (mode === 0) {
                var h = hitTest(mouse.x);
                cursorShape = (h === 1 || h === 2) ? Qt.SizeHorCursor : Qt.ArrowCursor;
                return;
            }
            var t = root.timeForX(mouse.x);
            var minGap = Math.min(0.1, root.durationSec);
            if (mode === 1)
                root.startSec = Math.min(t, root.endSec - minGap);
            else if (mode === 2)
                root.endSec = Math.max(t, root.startSec + minGap);
        }
        onPressed: function (mouse) {
            if (root.durationSec <= 0)
                return;
            mode = hitTest(mouse.x);
        }
        onReleased: { mode = 0; }
    }
}
