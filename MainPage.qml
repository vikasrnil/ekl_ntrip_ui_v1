import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    width: parent.width
    height: parent.height

    // ================= STATUS =================
    property string statusMsg: ""

    Timer {
        id: clearTimer
        interval: 1000
        repeat: false
        onTriggered: statusMsg = ""
    }

    function showMessage(msg) {
        statusMsg = msg
        clearTimer.restart()
    }

    Column {
        anchors.centerIn: parent
        spacing: 10
        width: parent.width * 0.8

        Text {
            text: "NTRIP CLIENT"
            font.pixelSize: 20
        }

        Text {
            text: statusMsg
            color: "blue"
        }

        // ================= INPUTS =================
        TextField { id: hostField; placeholderText: "Host" }
        TextField { id: portField; placeholderText: "Port" }

        // ================= FETCH =================
        Button {
            text: "Fetch Mount Points"
            onClicked: {
                if (hostField.text === "" || portField.text === "") {
                    showMessage("Enter Host & Port")
                    return
                }

                showMessage("Fetching...")
                ntripClient.fetchMountPoints(hostField.text, portField.text)
            }
        }

        // ================= MOUNT =================
        ComboBox {
            id: mountCombo
            model: []
        }

        TextField { id: userField; placeholderText: "User" }
        TextField {
            id: passwordField
            placeholderText: "Password"
            echoMode: TextInput.Password
        }

        // ================= CONNECT =================
        Button {
            text: "Connect"
            onClicked: {
                if (hostField.text === "" ||
                    portField.text === "" ||
                    mountCombo.currentText === "" ||
                    userField.text === "" ||
                    passwordField.text === "") {

                    showMessage("Fill all fields")
                    return
                }

                showMessage("Connecting...")

                ntripClient.connectToServer(
                    hostField.text,
                    portField.text,
                    mountCombo.currentText,
                    userField.text,
                    passwordField.text
                )
            }
        }

        // ================= DISCONNECT =================
        Button {
            text: "Disconnect"
            onClicked: {
                ntripClient.disconnectFromServer()
                showMessage("Disconnected")
            }
        }
    }

    // ================= SIGNALS =================
    Connections {
        target: ntripClient

        function onMountPointsReceived(list) {
            mountCombo.model = list

            if (list.length > 0) {
                mountCombo.currentIndex = 0
                showMessage("Mountpoints loaded")
            } else {
                showMessage("No mountpoints")
            }
        }

        function onConnectionStatus(s) {
            showMessage(s)
        }
    }
}
