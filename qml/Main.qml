import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtWebView

ApplicationWindow {
    id: root

    width: 430
    height: 860
    visible: true
    title: qsTr("TradeChart Lab")
    color: Material.background
    property bool controlsExpanded: root.width >= 360

    Material.theme: darkTheme.checked ? Material.Dark : Material.Light
    Material.accent: Material.Blue

    function refreshMarketData() {
        mobileController.refresh(symbolField.text, timeframeBox.currentIndex)
    }

    Component.onCompleted: {
        mobileController.setMarketStructureVisible(structureCheck.checked)
        refreshMarketData()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: mobileController.topSystemInset
        anchors.bottomMargin: mobileController.bottomSystemInset
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8

                Label {
                    text: root.width < 360
                        ? qsTr("TradeChart")
                        : qsTr("TradeChart Lab")
                    font.pixelSize: root.width < 360 ? 18 : 20
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Switch {
                    id: darkTheme
                    text: root.width < 360 ? "" : qsTr("Dark")
                    Accessible.name: qsTr("Dark theme")
                    checked: true
                    onToggled: mobileController.setDarkTheme(checked)
                }

                ToolButton {
                    visible: root.width <= root.height
                    text: controls.visible ? "▲" : "▼"
                    Accessible.name: controls.visible
                        ? qsTr("Hide controls")
                        : qsTr("Show controls")
                    onClicked: root.controlsExpanded = !root.controlsExpanded
                }
            }
        }

        Pane {
            id: controls
            visible: root.controlsExpanded && root.width <= root.height
            Layout.fillWidth: true
            padding: 10

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true

                    TextField {
                        id: symbolField
                        Layout.fillWidth: true
                        text: "AAPL"
                        placeholderText: qsTr("Symbol, for example AAPL")
                        maximumLength: 32
                        inputMethodHints: Qt.ImhUppercaseOnly
                        selectByMouse: true
                        onAccepted: root.refreshMarketData()
                    }

                    Button {
                        text: mobileController.busy ? qsTr("Loading…") : qsTr("Refresh")
                        enabled: !mobileController.busy
                        onClicked: root.refreshMarketData()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width < 620 ? 2 : 4
                    columnSpacing: 8
                    rowSpacing: 6

                    ComboBox {
                        id: timeframeBox
                        Layout.fillWidth: true
                        model: ["1 min", "5 min", "15 min", "1 hour", "1 day"]
                        currentIndex: 1
                    }

                    ComboBox {
                        id: styleBox
                        Layout.fillWidth: true
                        model: [qsTr("Candles"), qsTr("Line"), qsTr("Area")]
                        onActivated: mobileController.setChartStyle(currentIndex)
                    }

                    ComboBox {
                        id: indicatorBox
                        Layout.fillWidth: true
                        model: [
                            qsTr("No indicator"),
                            "SMA 20",
                            "EMA 20",
                            "VWAP",
                            "RSI 14",
                            "MACD 12/26/9",
                            qsTr("Rolling high 20"),
                            qsTr("Rolling low 20"),
                            qsTr("Volume SMA 20")
                        ]
                        currentIndex: 1
                        onActivated: mobileController.setIndicator(currentIndex)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("Fit chart")
                        onClicked: mobileController.fitChart()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width < 400 ? 2 : 4
                    columnSpacing: 10
                    rowSpacing: 6

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            text: qsTr("Last")
                            opacity: 0.7
                        }
                        Label {
                            Layout.fillWidth: true
                            text: mobileController.latestPrice
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            text: qsTr("Change")
                            opacity: 0.7
                        }
                        Label {
                            Layout.fillWidth: true
                            text: mobileController.priceChange
                            font.bold: true
                            elide: Text.ElideRight
                            color: text.startsWith("-")
                                ? Material.color(Material.Red)
                                : Material.color(Material.Green)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            text: qsTr("Range")
                            opacity: 0.7
                        }
                        Label {
                            Layout.fillWidth: true
                            text: mobileController.loadedRange
                            elide: Text.ElideRight
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            text: qsTr("Avg vol")
                            opacity: 0.7
                        }
                        Label {
                            Layout.fillWidth: true
                            text: mobileController.averageVolume
                            elide: Text.ElideRight
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    TextField {
                        id: twelveDataKey
                        Layout.fillWidth: true
                        placeholderText: mobileController.hasTwelveDataKey
                            ? qsTr("Twelve Data fallback enabled")
                            : qsTr("Optional Twelve Data API key")
                        echoMode: TextInput.Password
                        maximumLength: 256
                    }

                    Button {
                        text: mobileController.hasTwelveDataKey
                            ? qsTr("Replace")
                            : qsTr("Use key")
                        enabled: twelveDataKey.text.length > 0
                        onClicked: {
                            mobileController.setTwelveDataKey(twelveDataKey.text)
                            twelveDataKey.clear()
                        }
                    }
                }

                CheckBox {
                    id: structureCheck
                    text: qsTr("Show market-structure overlays")
                    checked: false
                    onToggled: mobileController.setMarketStructureVisible(checked)
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Yahoo Finance is queried first. The optional key remains only in memory for this session.")
                    wrapMode: Text.WordWrap
                    opacity: 0.65
                    font.pixelSize: 11
                }
            }
        }

        WebView {
            id: chartView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.width > root.height ? 180 : 280
            url: "file:///android_asset/web/mobile.html"

            onLoadingChanged: function(loadRequest) {
                if (loadRequest.status === WebView.LoadStartedStatus) {
                    chartReadyPoll.stop()
                    mobileController.beginChartLoad()
                } else if (loadRequest.status === WebView.LoadSucceededStatus) {
                    chartReadyPoll.attempts = 0
                    chartReadyPoll.start()
                } else if (loadRequest.status === WebView.LoadFailedStatus) {
                    chartReadyPoll.stop()
                    mobileController.reportWebViewError(
                        loadRequest.errorString || qsTr("unknown loading error"))
                }
            }

            Connections {
                target: mobileController

                function onExecuteChartJavaScript(script) {
                    chartView.runJavaScript(script, function(result) {
                        if (typeof result === "string" && result.length > 0)
                            mobileController.reportChartError(result)
                    })
                }
            }
        }

        Timer {
            id: chartReadyPoll
            interval: 150
            repeat: true
            property int attempts: 0

            onTriggered: {
                attempts += 1
                chartView.runJavaScript(
                    "typeof window.mobileChart?.receive === 'function'",
                    function(ready) {
                        if (ready === true) {
                            chartReadyPoll.stop()
                            mobileController.chartLoaded()
                        } else if (chartReadyPoll.attempts >= 50) {
                            chartReadyPoll.stop()
                            mobileController.reportChartError(
                                qsTr("Mobile chart bridge did not become ready."))
                        }
                    })
            }
        }

        Frame {
            Layout.fillWidth: true
            padding: 8

            RowLayout {
                anchors.fill: parent

                BusyIndicator {
                    running: mobileController.busy
                    visible: running
                    implicitWidth: 24
                    implicitHeight: 24
                }

                Label {
                    Layout.fillWidth: true
                    text: mobileController.status
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                }

                Label {
                    text: mobileController.source + " · " +
                          mobileController.barCount + " bars"
                    visible: root.width >= 400
                    elide: Text.ElideRight
                    Layout.maximumWidth: root.width * 0.38
                    opacity: 0.7
                    font.pixelSize: 11
                }
            }
        }
    }
}
