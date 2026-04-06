import QtQuick 2.15
import QtQuick.Controls 2.15

ApplicationWindow {
    visible: true
    width: 500
    height: 550

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: mainPage
    }

    Component {
        id: mainPage
        MainPage {
            stackView: stack
        }
    }

    Component {
        id: dataPage
        DataPage {
            stackView: stack
        }
    }
}
