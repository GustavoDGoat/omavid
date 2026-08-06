import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtMultimedia
import "Format.js" as Format

ApplicationWindow {
    id: win
    width: 960
    height: 680
    minimumWidth: 640
    minimumHeight: 460
    visible: true
    title: backend.source.toString() === "" ? "omacut" : "omacut — " + fileName(backend.source)
    readonly property bool hasVideo: backend.source.toString() !== ""
    readonly property bool audioOutputReady: audioOutput !== null
    property var audioOutput: null
    property string noticeText: ""
    property bool helpVisible: false
    readonly property string statusText: noticeText !== "" ? noticeText : backend.status

    Material.theme: Material.Dark
    Material.accent: "#FFD60A"
    color: "#0e0e10"

    function fileName(url) {
        var s = url.toString();
        return s === "" ? "" : decodeURIComponent(s.substring(s.lastIndexOf('/') + 1));
    }
    function showNotice(text) {
        noticeText = text;
        noticeTimer.restart();
    }
    function openVideo() {
        backend.openVideoDialog();
    }
    function exportVideo() {
        if (!win.hasVideo || backend.duration <= 0 || backend.busy)
            return;
        backend.exportDialog(trimBar.startSec, trimBar.endSec);
    }
    function ensureAudioOutput() {
        if (audioOutput === null && win.hasVideo)
            audioOutput = audioOutputComponent.createObject(win);
    }
    function releaseAudioOutput() {
        if (audioOutput === null)
            return;
        var oldAudioOutput = audioOutput;
        audioOutput = null;
        oldAudioOutput.destroy();
    }
    function togglePlay() {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        ensureAudioOutput();
        if (player.priming)
            player.finishPriming();
        if (player.playbackState === MediaPlayer.PlayingState) {
            player.pause();
            return;
        }
        // The pause-at-end clamp rounds to whole milliseconds and the player
        // snaps seeks to frames, so a finished clip can rest a fraction of a
        // millisecond before endSec. Treat anything within 10 ms of the end
        // as "at the end" or play would instantly re-pause instead of
        // restarting from the trim start.
        var pos = player.position / 1000;
        if (pos < trimBar.startSec || pos >= trimBar.endSec - 0.01)
            player.position = Math.round(trimBar.startSec * 1000);
        player.play();
    }
    function movePlayheadTo(seconds) {
        if (player.priming)
            player.finishPriming();
        trimBar.playheadSec = seconds;
        player.position = Math.round(seconds * 1000);
    }
    function seekBy(seconds) {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        // The playhead lives inside the trim, same as scrubbing and preview.
        movePlayheadTo(Math.max(trimBar.startSec, Math.min(trimBar.playheadSec + seconds, trimBar.endSec)));
    }
    // Both edges park the playhead on themselves, so you see the frame you just
    // trimmed to — the same thing dragging a handle does. While zoomed, the
    // edges stop at the zoom window instead of the video bounds.
    function moveTrimStartTo(seconds) {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        var minGap = Math.min(0.1, backend.duration);
        trimBar.startSec = Math.max(trimBar.windowStart, Math.min(seconds, trimBar.endSec - minGap));
        movePlayheadTo(trimBar.startSec);
    }
    function moveTrimEndTo(seconds) {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        var minGap = Math.min(0.1, backend.duration);
        trimBar.endSec = Math.min(trimBar.windowEnd, Math.max(seconds, trimBar.startSec + minGap));
        movePlayheadTo(trimBar.endSec);
    }

    Component.onCompleted: ensureAudioOutput()
    onHasVideoChanged: {
        if (hasVideo) {
            ensureAudioOutput();
        } else {
            player.stop();
            releaseAudioOutput();
        }
    }
    onClosing: {
        player.stop();
        releaseAudioOutput();
        Qt.quit();
    }

    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: togglePlay()
    }

    Shortcut {
        sequence: "Alt+Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: moveTrimStartTo(trimBar.playheadSec)
    }

    Shortcut {
        sequence: "Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: seekBy(-1)
    }

    Shortcut {
        sequence: "Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: seekBy(1)
    }

    Shortcut {
        sequence: "Shift+Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: seekBy(-5)
    }

    Shortcut {
        sequence: "Shift+Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: seekBy(5)
    }

    Shortcut {
        sequence: "Alt+Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: seekBy(-0.2)
    }

    Shortcut {
        sequence: "Alt+Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: seekBy(0.2)
    }

    Shortcut {
        sequence: "Ctrl+Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: moveTrimEndTo(trimBar.endSec - 5)
    }

    Shortcut {
        sequence: "Ctrl+Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: moveTrimEndTo(trimBar.endSec + 5)
    }

    Shortcut {
        sequence: "Z"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0
        onActivated: {
            trimBar.toggleZoom();
            backend.requestThumbs(trimBar.windowStart, trimBar.windowEnd);
        }
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !backend.busy
        onActivated: exportVideo()
    }

    Shortcut {
        sequence: "?"
        context: Qt.ApplicationShortcut
        onActivated: win.helpVisible = !win.helpVisible
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible)
                win.helpVisible = false;
            else
                openVideo();
        }
    }

    MediaPlayer {
        id: player
        source: backend.source
        videoOutput: videoOut
        audioOutput: win.audioOutput

        // Render the opening frame on load instead of showing black. Playback
        // starts muted and stops as soon as VideoOutput receives a frame.
        property bool primed: false
        property bool priming: false

        function startPriming() {
            if (primed || priming || backend.source.toString() === "")
                return;
            win.ensureAudioOutput();
            primed = true;
            priming = true;
            position = 0;
            play();
            primeFallback.restart();
        }

        function finishPriming() {
            if (!priming)
                return;
            primeFallback.stop();
            pause();
            position = 0;
            priming = false;
        }

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                startPriming();
        }
        onPositionChanged: {
            if (priming && position > 0) {
                finishPriming();
                return;
            }
            // Stop at the trim end, like a clip preview.
            if (playbackState === MediaPlayer.PlayingState && position / 1000 >= trimBar.endSec) {
                pause();
                position = Math.round(trimBar.endSec * 1000);
            }
            if (!trimBar.interacting)
                trimBar.playheadSec = position / 1000;
        }
    }

    Component {
        id: audioOutputComponent
        AudioOutput {
            muted: player.priming
        }
    }

    Timer {
        id: primeFallback
        interval: 250
        repeat: false
        onTriggered: player.finishPriming()
    }

    Timer {
        id: noticeTimer
        interval: 5000
        repeat: false
        onTriggered: win.noticeText = ""
    }

    component IconButton: Rectangle {
        id: iconButton
        implicitWidth: 44
        implicitHeight: 44
        radius: 22

        property string iconName: "play"
        property color iconColor: "white"
        property color buttonColor: "#2c2c2f"
        property string tipText: ""
        signal clicked()

        color: buttonColor
        opacity: enabled ? 1 : 0.45

        HoverHandler { id: iconHover }
        ToolTip.visible: iconHover.hovered && tipText !== ""
        ToolTip.text: tipText

        Canvas {
            id: iconCanvas
            anchors.centerIn: parent
            width: 24
            height: 24

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.fillStyle = iconButton.iconColor;
                ctx.strokeStyle = iconButton.iconColor;
                ctx.lineWidth = 2.4;
                ctx.lineCap = "round";
                ctx.lineJoin = "round";

                if (iconButton.iconName === "pause") {
                    ctx.fillRect(7, 5, 4, 14);
                    ctx.fillRect(14, 5, 4, 14);
                } else if (iconButton.iconName === "play") {
                    ctx.beginPath();
                    ctx.moveTo(8, 5);
                    ctx.lineTo(8, 19);
                    ctx.lineTo(19, 12);
                    ctx.closePath();
                    ctx.fill();
                } else if (iconButton.iconName === "download") {
                    ctx.beginPath();
                    ctx.moveTo(12, 4);
                    ctx.lineTo(12, 14);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(7, 10);
                    ctx.lineTo(12, 15);
                    ctx.lineTo(17, 10);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(6, 20);
                    ctx.lineTo(18, 20);
                    ctx.stroke();
                }
            }

            Connections {
                target: iconButton
                function onIconNameChanged() { iconCanvas.requestPaint(); }
                function onIconColorChanged() { iconCanvas.requestPaint(); }
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: iconButton.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: iconButton.clicked()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: win.hasVideo ? 16 : 0
        spacing: 14

        // --- video preview ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: win.hasVideo ? 12 : 0
            color: "black"
            clip: true

            VideoOutput {
                id: videoOut
                anchors.fill: parent
            }
            Connections {
                target: videoOut.videoSink
                function onVideoFrameChanged(frame) {
                    player.finishPriming();
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: openVideo()
            }

            Button {
                id: openVideoButton
                anchors.centerIn: parent
                visible: !win.hasVideo
                text: "Open a video"
                highlighted: true
                focusPolicy: Qt.NoFocus
                font.pixelSize: 18
                Material.foreground: "black"
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
                contentItem: Label {
                    text: openVideoButton.text
                    font: openVideoButton.font
                    color: "black"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: openVideo()
            }
        }

        // --- timeline ---
        RowLayout {
            visible: win.hasVideo
            Layout.fillWidth: true
            spacing: 10

            IconButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                iconName: player.playbackState === MediaPlayer.PlayingState && !player.priming ? "pause" : "play"
                tipText: player.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"
                enabled: backend.duration > 0
                onClicked: togglePlay()
            }

            TrimBar {
                id: trimBar
                objectName: "trimBar"
                Layout.fillWidth: true
                durationSec: backend.duration
                thumbCount: backend.thumbCount
                thumbReadyCount: backend.thumbReadyCount
                thumbRevision: backend.thumbRevision
                onScrub: (seconds) => player.position = Math.round(seconds * 1000)
            }

            IconButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                iconName: "download"
                tipText: "Export"
                enabled: backend.duration > 0 && !backend.busy
                onClicked: exportVideo()
            }
        }

        // --- status line ---
        Item {
            visible: win.hasVideo
            Layout.fillWidth: true
            Layout.preferredHeight: 26

            Label {
                anchors.centerIn: parent
                width: parent.width
                visible: win.statusText !== ""
                text: win.statusText
                color: win.noticeText !== "" ? "#FFD60A" : "#b8b8bc"
                font.pixelSize: 13
                font.family: "monospace"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }

            Label {
                anchors.centerIn: parent
                visible: win.statusText === "" && backend.duration > 0 && !trimBar.trimmingRange
                textFormat: Text.StyledText
                text: Format.fmt(trimBar.playheadSec) + " (" + Format.fmt(trimBar.endSec - trimBar.startSec) + ")"
                    + (trimBar.zoomed ? " · <font color=\"#FFD60A\">zoomed</font>" : "")
                color: "#d6d6da"
                font.pixelSize: 13
                font.family: "monospace"
            }
        }
    }

    // --- subtle help toggle in the corner ---
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: 24
        height: 24
        radius: 12
        color: helpHover.hovered ? "#2c2c2f" : "transparent"

        Text {
            anchors.centerIn: parent
            text: "?"
            color: helpHover.hovered ? "white" : "#7a7a80"
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        HoverHandler {
            id: helpHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: win.helpVisible = !win.helpVisible
        }
    }

    // --- hotkey overlay ---
    Rectangle {
        visible: win.helpVisible
        anchors.fill: parent
        color: "#000000cc"

        MouseArea {
            anchors.fill: parent
            onClicked: win.helpVisible = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: helpColumn.width + 56
            height: helpColumn.height + 48
            radius: 12
            color: "#1c1c1e"

            Column {
                id: helpColumn
                anchors.centerIn: parent
                spacing: 10

                Label {
                    text: "Keyboard shortcuts"
                    color: "white"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    bottomPadding: 8
                }

                Repeater {
                    model: [
                        { keys: "Space", action: "Play / pause" },
                        { keys: "← / →", action: "Move playhead 1s" },
                        { keys: "Shift ← / →", action: "Move playhead 5s" },
                        { keys: "Alt ← / →", action: "Move playhead 0.2s" },
                        { keys: "Ctrl ← / →", action: "Move trim end 5s" },
                        { keys: "Alt Space", action: "Trim start to playhead" },
                        { keys: "Z", action: "Zoom the selection" },
                        { keys: "Enter", action: "Export" },
                        { keys: "Esc", action: "Open a video" },
                        { keys: "?", action: "Show these shortcuts" }
                    ]
                    delegate: Row {
                        spacing: 18
                        Label {
                            width: 110
                            horizontalAlignment: Text.AlignRight
                            text: modelData.keys
                            color: "#FFD60A"
                            font.pixelSize: 13
                            font.family: "monospace"
                        }
                        Label {
                            text: modelData.action
                            color: "#d6d6da"
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: backend
        function onInfoChanged() {
            win.noticeText = "";
            noticeTimer.stop();
            // Reset priming too, or a video opened mid-prime would stay black:
            // startPriming() bails while priming is still true.
            primeFallback.stop();
            player.priming = false;
            player.primed = false;
            trimBar.zoomed = false;
            trimBar.startSec = 0;
            trimBar.endSec = backend.duration;
            trimBar.playheadSec = 0;
        }
        function onExportDone(path) {
            win.showNotice("Saved " + path);
        }
        function onExportFailed(message) {
            win.showNotice("Export failed: " + message);
        }
        function onLoadError(message) {
            win.showNotice("Cannot open video: " + message);
        }
    }
}
