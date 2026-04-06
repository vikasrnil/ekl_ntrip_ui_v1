import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    width: parent.width
    height: parent.height

    property bool isDisconnected: false   // safety flag

    Column {
        anchors.centerIn: parent
        spacing: 20

        Text {
            text: "Live GNSS Data"
            font.pixelSize: 20
        }

        Text {
            id: dataText
            text: "Waiting..."
            font.pixelSize: 16
        }

        // ================= DISCONNECT =================
        Button {
            text: "Disconnect"
            onClicked: {
                if (!isDisconnected) {
                    ntripClient.disconnectClient()
                    isDisconnected = true
                    dataText.text = "Disconnected"
                }
            }
        }

        // ================= BACK =================
        Button {
            text: "Back"
            onClicked: {
                if (!isDisconnected) {
                    ntripClient.disconnectClient()
                    isDisconnected = true
                }

                stack.pop()
            }
        }
    }

    // ================= DATA =================
    Connections {
        target: ntripClient

        function onDataUpdated(line) {
            if (!isDisconnected)
                dataText.text = line
        }

        function onConnectionStatus(s) {
            if (s === "Disconnected")
                isDisconnected = true
        }
    }
}
