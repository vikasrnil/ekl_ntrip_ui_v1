import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    width: parent.width
    height: parent.height

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

        Text { text: "NTRIP CLIENT"; font.pixelSize: 20 }
        Text { text: statusMsg; color: "blue" }

        TextField { id: hostField; placeholderText: "Host" }
        TextField { id: portField; placeholderText: "Port" }

        Button {
            text: "Fetch Mount Points"
            onClicked: {
                if (hostField.text === "" || portField.text === "") {
                    showMessage("Enter Host & Port")
                    return
                }
                ntripClient.fetchMountPoints(hostField.text, parseInt(portField.text))
            }
        }

        ComboBox { id: mountCombo; model: [] }

        TextField { id: userField; placeholderText: "User" }
        TextField { id: passwordField; placeholderText: "Password"; echoMode: TextInput.Password }

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

                ntripClient.connectToMountPoint(
                    hostField.text,
                    parseInt(portField.text),
                    mountCombo.currentText,
                    auth
                )
            }
        }

        Button {
            text: "Disconnect"
            onClicked: {
                ntripClient.disconnectClient()
            }
        }
    }

    Connections {
        target: ntripClient

        function onMountPointsReceived(list) {
            mountCombo.model = list
            showMessage(list.length > 0 ? "Mountpoints loaded" : "No mountpoints")
        }

        function onConnectionStatus(s) {
            showMessage(s)
        }
    }
}
