import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    Column {
        anchors.centerIn: parent
        spacing: 20

        Text {
            text: "Live GNSS Data"
            font.pixelSize: 20
        }

        // Parsed one-line data
        Text {
            id: dataText
            text: "Waiting..."
            font.pixelSize: 16
        }

        // DISCONNECT BUTTON
        Button {
            text: "Disconnect"
            onClicked: {
                ntripClient.disconnectFromServer()
                dataText.text = "Disconnected"
            }
        }

        // BACK BUTTON (also disconnect)
        Button {
            text: "Back"
            onClicked: {
                ntripClient.disconnectFromServer()
                stack.pop()
            }
        }
    }

    // RECEIVE DATA FROM C++
    Connections {
        target: ntripClient

        function onDataUpdated(line) {
            dataText.text = line
        }
    }
}
