import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    width: parent.width
    height: parent.height

    property var stackView
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

        // ================= INPUT =================
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
                ntripClient.fetchMountPoints(
                    hostField.text,
                    parseInt(portField.text)
                )
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

                var auth = userField.text + ":" + passwordField.text
                showMessage("Connecting...")

                ntripClient.connectToMountPoint(
                    hostField.text,
                    parseInt(portField.text),
                    mountCombo.currentText,
                    auth
                )
            }
        }

        // ================= DISCONNECT =================
        Button {
            text: "Disconnect"
            onClicked: {
                ntripClient.disconnectClient()
                showMessage("Disconnected")
            }
        }
    }

    // ================= BACKEND SIGNALS =================
    Connections {
        target: ntripClient

        function onMountPointsReceived(list) {
            mountCombo.model = list
            showMessage(list.length > 0 ? "Mountpoints loaded" : "No mountpoints")
        }

        function onConnectionStatus(s) {
            showMessage(s)

            // ✅ NAVIGATE TO DATA PAGE
            if (s === "Connected") {
                stackView.push("DataPage.qml")
            }
        }
    }
}
