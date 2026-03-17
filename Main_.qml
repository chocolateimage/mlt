import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

View3D {
    width: 800
    height: 600

    environment: SceneEnvironment {
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.VeryHigh
        backgroundMode: SceneEnvironment.Color
        clearColor: "blue"
    }

    PerspectiveCamera {
        position: Qt.vector3d(0, 200, 300)
        eulerRotation.x: -30
    }

    DirectionalLight {
        eulerRotation.x: -30
        eulerRotation.y: -70
    }

    FontMetrics {
        id: fontMetrics
        font.pointSize: 64
    }

    Model {
        position: Qt.vector3d(0, -200, -16)
        source: "#Cylinder"
        scale: Qt.vector3d(2, 0.2, 1)
        materials: [
            PrincipledMaterial {
                baseColor: "red"
            }
        ]
    }

    Model {
        id: a
        geometry: ExtrudedTextGeometry {
            id: textGeometry
            text: "Hello world"

            font {
                pointSize: 64
            }
            scale: 64
            depth: 16
        }

        position: Qt.vector3d(fontMetrics.advanceWidth("Hello world") / -2, 0, -16)

        materials: [
            PrincipledMaterial {
                baseColor: "red"
            }
        ]
    }
}
