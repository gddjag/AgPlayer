import QtQuick

Item {
    id: root

    required property Item sourceItem
    property real intensity: 0.0
    property real spill: 0.0
    property real radius: 1.0
    property real threshold: 0.34
    readonly property real boundedRadius:
        Math.max(0.2, Math.min(2.0, radius))
    readonly property real textureScale:
        Math.min(0.25,
                 512.0 / Math.max(1, width),
                 512.0 / Math.max(1, height))
    readonly property int textureWidth:
        Math.min(512, Math.max(1, Math.ceil(width * textureScale)))
    readonly property int textureHeight:
        Math.min(512, Math.max(1, Math.ceil(height * textureScale)))

    visible: sourceItem !== null && intensity > 0 && spill > 0
             && width > 0 && height > 0

    ShaderEffect {
        id: horizontalPass
        width: root.textureWidth
        height: root.textureHeight

        property var source: root.sourceItem
        property real threshold: root.threshold
        property vector2d texelStep:
            Qt.vector2d(root.boundedRadius / width, 0.0)

        fragmentShader: "qrc:/terrain/shaders/terrain_glow_extract.frag.qsb"
    }

    ShaderEffectSource {
        id: horizontalTexture
        sourceItem: horizontalPass
        hideSource: true
        live: true
        recursive: false
        textureSize: Qt.size(root.textureWidth, root.textureHeight)
        format: ShaderEffectSource.RGBA8
        samples: 0
        mipmap: false
        smooth: true
        visible: false
    }

    ShaderEffect {
        anchors.fill: parent

        property var source: horizontalTexture
        property var terrainSource: root.sourceItem
        property real glowIntensity:
            Math.max(0.0, Math.min(2.0, root.intensity * root.spill))
        property real threshold: root.threshold
        property vector2d texelStep:
            Qt.vector2d(0.0, root.boundedRadius / root.textureHeight)

        fragmentShader: "qrc:/terrain/shaders/terrain_glow_blur.frag.qsb"
    }
}
