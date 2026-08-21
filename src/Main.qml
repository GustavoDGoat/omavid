import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtMultimedia
import "Format.js" as Format

ApplicationWindow {
    id: win
    width: 1180
    height: 680
    minimumWidth: 860
    minimumHeight: 460
    visible: true
    title: backend.source.toString() === "" ? "omavid" : "omavid — " + fileName(backend.source)
    readonly property bool hasVideo: backend.source.toString() !== ""
    readonly property color accent: backend.themeAccent
    readonly property color accentForeground: backend.themeAccentForeground
    readonly property bool audioOutputReady: audioOutput !== null
    property var audioOutput: null
    property string noticeText: ""
    property bool helpVisible: false
    property bool quitConfirmVisible: false
    readonly property string statusText: noticeText !== "" ? noticeText : backend.status

    // Tracks unexported work across the whole playlist: a trim or a reorder
    // bumps backend.clipRevision, and exporting records the revision it saved.
    // Plain value (not a binding) so it snapshots the revision at export time.
    property int lastExportedClipRevision: 0
    readonly property bool trimDirty: hasVideo
        && (backend.anyClipTrimmed || backend.anyAudioWork)
        && backend.clipRevision !== lastExportedClipRevision

    // Guards the write-back of the TrimBar to the model, so switching clips
    // (which reloads the trim bar) doesn't echo the values back as edits.
    property bool syncingTrim: false
    property bool syncingAudio: false

    Material.theme: Material.Dark
    Material.accent: win.accent
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
    function pushTrim() {
        if (!win.syncingTrim)
            backend.setClipTrim(backend.currentIndex, trimBar.startSec, trimBar.endSec);
    }
    function pushClipAudioTrim() {
        if (!win.syncingAudio)
            backend.setClipAudioTrim(backend.currentIndex, clipAudioBar.startSec, clipAudioBar.endSec);
    }
    function syncClipAudioBar() {
        win.syncingAudio = true;
        clipAudioBar.startSec = backend.clipAudioStart;
        clipAudioBar.endSec = backend.clipAudioEnd;
        win.syncingAudio = false;
    }
    function exportVideo() {
        if (!win.hasVideo || backend.duration <= 0 || backend.busy)
            return;
        backend.exportDialog(trimBar.startSec, trimBar.endSec);
    }
    function exportMergeVideo() {
        if (backend.clipCount < 2 || backend.busy)
            return;
        backend.exportMergeDialog();
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
    property bool quitting: false
    function requestQuit() {
        if (trimDirty) {
            if (player.playbackState === MediaPlayer.PlayingState)
                player.pause();
            quitConfirmVisible = true;
            return;
        }
        forceQuit();
    }
    // Closing the window is what reliably ends the app (quitOnLastWindowClosed);
    // Qt.quit() alone has proven ignorable in a live session, so it's only the
    // backstop. The quitting flag stops onClosing from re-asking on the way out.
    function forceQuit() {
        quitting = true;
        player.stop();
        releaseAudioOutput();
        win.close();
        Qt.quit();
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
    onClosing: (close) => {
        if (win.quitting)
            return;
        if (win.trimDirty) {
            close.accepted = false;
            if (player.playbackState === MediaPlayer.PlayingState)
                player.pause();
            win.quitConfirmVisible = true;
            return;
        }
        forceQuit();
    }

    // The playback and trim shortcuts go quiet while the quit confirmation is
    // up — a disabled Shortcut also stops swallowing its key, which lets the
    // dialog's own keyboard navigation receive the arrows, Space and Enter.
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: togglePlay()
    }

    Shortcut {
        sequence: "Ctrl+Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: moveTrimStartTo(trimBar.playheadSec)
    }

    Shortcut {
        sequence: "Alt+Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: moveTrimEndTo(trimBar.playheadSec)
    }

    Shortcut {
        sequence: "Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: seekBy(-1)
    }

    Shortcut {
        sequence: "Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: seekBy(1)
    }

    Shortcut {
        sequence: "Shift+Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: seekBy(-5)
    }

    Shortcut {
        sequence: "Shift+Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: seekBy(5)
    }

    Shortcut {
        sequence: "Alt+Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: seekBy(-0.2)
    }

    Shortcut {
        sequence: "Alt+Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible
        onActivated: seekBy(0.2)
    }

    Shortcut {
        sequence: "Z"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !win.quitConfirmVisible
        onActivated: {
            trimBar.toggleZoom();
            backend.requestThumbs(trimBar.windowStart, trimBar.windowEnd);
        }
    }

    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !backend.busy
        onActivated: {
            win.quitConfirmVisible = false;
            exportVideo();
        }
    }

    Shortcut {
        sequence: "Ctrl+M"
        context: Qt.ApplicationShortcut
        enabled: backend.clipCount >= 2 && !backend.busy && !win.quitConfirmVisible
        onActivated: exportMergeVideo()
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        enabled: !win.quitConfirmVisible
        onActivated: openVideo()
    }

    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!win.quitConfirmVisible)
                requestQuit();
        }
    }

    Shortcut {
        sequence: "?"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!win.quitConfirmVisible)
                win.helpVisible = !win.helpVisible;
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.quitConfirmVisible)
                win.quitConfirmVisible = false;
            else if (win.helpVisible)
                win.helpVisible = false;
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

    component DialogButton: Rectangle {
        id: dialogButton
        width: dialogButtonLabel.implicitWidth + 28
        height: 34
        radius: 8

        property string text: ""
        property bool primary: false
        signal clicked()

        color: primary ? win.accent : "#2c2c2f"
        border.color: activeFocus ? (primary ? win.accentForeground : win.accent) : "transparent"
        border.width: activeFocus ? 2 : 0

        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Keys.onSpacePressed: clicked()

        Label {
            id: dialogButtonLabel
            anchors.centerIn: parent
            text: dialogButton.text
            color: dialogButton.primary ? win.accentForeground : "white"
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: dialogButton.clicked()
        }
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

    // A tiny icon-less button for per-clip reorder/remove/audio actions.
    component RowButton: Rectangle {
        id: rowButton
        width: 22
        height: 22
        radius: 5
        property string glyph: ""
        property string tip: ""
        property color glyphColor: "#c0c0c4"
        signal clicked()

        color: rowHover.hovered && enabled ? "#3a3a3e" : "transparent"
        opacity: enabled ? 1 : 0.3

        HoverHandler { id: rowHover; enabled: rowButton.enabled }
        ToolTip.visible: rowHover.hovered && tip !== ""
        ToolTip.text: tip
        Text {
            anchors.centerIn: parent
            text: rowButton.glyph
            color: rowButton.glyphColor
            font.pixelSize: 14
        }
        MouseArea {
            anchors.fill: parent
            enabled: rowButton.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: rowButton.clicked()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- playlist sidebar ---
        Rectangle {
            id: sidebar
            visible: backend.clipCount > 0
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: "#141416"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Label {
                    text: "Videos (" + backend.clipCount + ")"
                    color: "#d6d6da"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                ListView {
                    id: clipList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: backend.clips
                    clip: true
                    spacing: 6

                    delegate: Rectangle {
                        id: clipRow
                        width: ListView.view.width
                        height: 56
                        radius: 8
                        color: index === backend.currentIndex ? "#2c2c2f" : "transparent"
                        border.color: index === backend.currentIndex ? win.accent : "transparent"
                        border.width: index === backend.currentIndex ? 1 : 0

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.selectClip(index)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 6

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: model.name
                                    color: "white"
                                    elide: Text.ElideRight
                                    font.pixelSize: 12
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: model.trimmed
                                        ? Format.fmt(model.startSec) + " – " + Format.fmt(model.endSec)
                                        : Format.fmt(model.duration)
                                    color: model.trimmed ? win.accent : "#9a9aa0"
                                    elide: Text.ElideRight
                                    font.pixelSize: 10
                                    font.family: "monospace"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: model.hasAudio
                                    text: "♪ " + model.audioName
                                    color: win.accent
                                    elide: Text.ElideRight
                                    font.pixelSize: 10
                                }
                            }

                            Row {
                                spacing: 2
                                RowButton {
                                    glyph: "↑"
                                    tip: "Move up"
                                    enabled: index > 0
                                    onClicked: backend.moveClip(index, index - 1)
                                }
                                RowButton {
                                    glyph: "↓"
                                    tip: "Move down"
                                    enabled: index < backend.clipCount - 1
                                    onClicked: backend.moveClip(index, index + 1)
                                }
                                RowButton {
                                    glyph: "♪"
                                    tip: model.hasAudio ? "Remove audio" : "Attach audio"
                                    glyphColor: model.hasAudio ? win.accent : "#c0c0c4"
                                    onClicked: model.hasAudio ? backend.detachAudio(index) : backend.attachAudioDialog(index)
                                }
                                RowButton {
                                    glyph: "×"
                                    tip: "Remove clip"
                                    onClicked: backend.removeClip(index)
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        Layout.fillWidth: true
                        text: "Add videos"
                        onClicked: openVideo()
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "Merge"
                        highlighted: true
                        Material.foreground: win.accentForeground
                        enabled: backend.clipCount >= 2 && !backend.busy
                        onClicked: exportMergeVideo()
                    }
                }
            }
        }

        // --- main area ---
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

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
                        Material.foreground: win.accentForeground
                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }
                        contentItem: Label {
                            text: openVideoButton.text
                            font: openVideoButton.font
                            color: win.accentForeground
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
                        accent: win.accent
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

                // --- audio ---
                ColumnLayout {
                    visible: win.hasVideo
                    Layout.fillWidth: true
                    spacing: 10

                    // per-clip replacement audio editor
                    ColumnLayout {
                        visible: backend.clipHasAudio
                        Layout.fillWidth: true
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: "Clip audio: " + backend.clipAudioName
                                color: win.accent
                                elide: Text.ElideMiddle
                                font.pixelSize: 12
                            }
                            RowButton {
                                glyph: "×"
                                tip: "Remove audio"
                                onClicked: backend.detachAudio(backend.currentIndex)
                            }
                        }

                        AudioBar {
                            id: clipAudioBar
                            Layout.fillWidth: true
                            durationSec: backend.clipAudioDuration
                            accent: win.accent
                        }
                    }

                    // merge audio track
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "Audio track"
                                color: "#d6d6da"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            CheckBox {
                                text: "Mute video audio"
                                checked: backend.muteVideoAudio
                                onCheckedChanged: backend.setMuteVideoAudio(checked)
                            }
                            Button {
                                text: "Add audio"
                                enabled: !backend.busy
                                onClicked: backend.addAudioDialog()
                            }
                        }

                        AudioTrack {
                            Layout.fillWidth: true
                            model: backend.audioTrack
                            totalDuration: backend.mergeDuration
                            accent: win.accent
                        }
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
                        color: win.noticeText !== "" ? win.accent : "#b8b8bc"
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
                            + (trimBar.zoomed ? " · <font color=\"" + win.accent + "\">zoomed</font>" : "")
                        color: "#d6d6da"
                        font.pixelSize: 13
                        font.family: "monospace"
                    }
                }
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
                        { keys: "Ctrl Space", action: "Trim start to playhead" },
                        { keys: "Alt Space", action: "Trim end to playhead" },
                        { keys: "Z", action: "Zoom the selection" },
                        { keys: "Ctrl O", action: "Open videos" },
                        { keys: "Ctrl S", action: "Export this clip" },
                        { keys: "Ctrl M", action: "Merge and export" },
                        { keys: "Q", action: "Quit" },
                        { keys: "?", action: "Show these shortcuts" }
                    ]
                    delegate: Row {
                        spacing: 18
                        Label {
                            width: 110
                            horizontalAlignment: Text.AlignRight
                            text: modelData.keys
                            color: win.accent
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

    // --- quit confirmation ---
    Rectangle {
        visible: win.quitConfirmVisible
        anchors.fill: parent
        color: "#000000cc"
        onVisibleChanged: {
            if (visible)
                quitExportButton.forceActiveFocus();
        }

        MouseArea {
            anchors.fill: parent
            onClicked: win.quitConfirmVisible = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: quitColumn.width + 64
            height: quitColumn.height + 48
            radius: 12
            color: "#1c1c1e"

            MouseArea {
                anchors.fill: parent
            }

            Column {
                id: quitColumn
                anchors.centerIn: parent
                spacing: 8

                Label {
                    text: "Unexported trim"
                    color: "white"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Label {
                    text: "Your trim hasn't been exported. Quit anyway?"
                    color: "#d6d6da"
                    font.pixelSize: 13
                    bottomPadding: 12
                }

                Row {
                    anchors.right: parent.right
                    spacing: 10

                    DialogButton {
                        id: quitCancelButton
                        text: "Cancel"
                        KeyNavigation.left: quitExportButton
                        KeyNavigation.right: quitQuitButton
                        KeyNavigation.tab: quitQuitButton
                        KeyNavigation.backtab: quitExportButton
                        onClicked: win.quitConfirmVisible = false
                    }
                    DialogButton {
                        id: quitQuitButton
                        text: "Quit"
                        KeyNavigation.left: quitCancelButton
                        KeyNavigation.right: quitExportButton
                        KeyNavigation.tab: quitExportButton
                        KeyNavigation.backtab: quitCancelButton
                        onClicked: forceQuit()
                    }
                    DialogButton {
                        id: quitExportButton
                        text: "Export"
                        primary: true
                        KeyNavigation.left: quitQuitButton
                        KeyNavigation.right: quitCancelButton
                        KeyNavigation.tab: quitCancelButton
                        KeyNavigation.backtab: quitQuitButton
                        onClicked: {
                            win.quitConfirmVisible = false;
                            exportVideo();
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: trimBar
        function onStartSecChanged() { win.pushTrim(); }
        function onEndSecChanged() { win.pushTrim(); }
    }

    Connections {
        target: clipAudioBar
        function onStartSecChanged() { win.pushClipAudioTrim(); }
        function onEndSecChanged() { win.pushClipAudioTrim(); }
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
            win.syncingTrim = true;
            trimBar.startSec = backend.clipStartSec;
            trimBar.endSec = backend.clipEndSec;
            trimBar.playheadSec = 0;
            win.syncingTrim = false;
            win.syncClipAudioBar();
        }
        function onClipsChanged() { win.syncClipAudioBar(); }
        function onExportDone(path) {
            win.showNotice("Saved " + path);
            if (backend.clipCount === 1)
                win.lastExportedClipRevision = backend.clipRevision;
        }
        function onMergeDone(path) {
            win.showNotice("Saved " + path);
            win.lastExportedClipRevision = backend.clipRevision;
        }
        function onExportFailed(message) {
            win.showNotice("Export failed: " + message);
        }
        function onLoadError(message) {
            win.showNotice("Cannot open video: " + message);
        }
    }
}
