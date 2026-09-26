import QtQuick
import AgPlayer

Text {
    id: root

    property string artist: ""
    property string album: ""
    property var tags: []

    function normalizedTags() {
        var result = []
        var values = tags || []
        for (var index = 0; index < values.length; ++index) {
            var value = String(values[index] || "").trim()
            if (value.length > 0)
                result.push(value)
        }
        return result
    }

    text: {
        var parts = [artist || qsTr("未知艺术家"),
                     album || qsTr("未知专辑")]
        return parts.concat(normalizedTags()).join(" · ")
    }
    color: Theme.secondaryText
    font.family: Theme.fontPrimary
    elide: Text.ElideRight
}
