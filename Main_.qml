import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

View3D {
    width: 800
    height: 600

    environment: SceneEnvironment {
        id: sceneEnvironment

        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.VeryHigh
    }

    Node {
        objectName: "viewNode"
        PerspectiveCamera {
            objectName: "Camera"
            position: Qt.vector3d(0, 200, 300)
            eulerRotation.x: -30
        }

        DirectionalLight {
            objectName: "Light"
            eulerRotation.x: -30
            eulerRotation.y: -70
            castsShadow: true

            color: Qt.rgba(156 / 255, 167 / 255, 1, 1.0)
            brightness: 1
            ambientColor: Qt.rgba(.2, .2, .2, 1)

            SequentialAnimation on eulerRotation.x {
                objectName: "animationlight"
                loops: Animation.Infinite
                NumberAnimation {
                    duration: 1000
                    from: 0
                    to: 360
                }
            }
        }

        DirectionalLight {
            objectName: "Light2"
            eulerRotation.x: -30
            eulerRotation.y: -70
            castsShadow: true
            color: Qt.rgba(255 / 255, 134 / 255, 128 / 255, 1.0)
            brightness: 1
            ambientColor: Qt.rgba(.2, .2, .2, 1)

            SequentialAnimation on eulerRotation.x {
                objectName: "lightanim2"
                loops: Animation.Infinite
                NumberAnimation {
                    duration: 1000
                    from: -180
                    to: 180
                }
            }
        }

        // Model {
        //     position: Qt.vector3d(0, -200, 0)
        //     source: "#Cylinder"
        //     scale: Qt.vector3d(2, 0.2, 1)
        //     materials: [
        //         PrincipledMaterial {
        //             baseColor: "red"
        //         }
        //     ]

        //     SequentialAnimation on y {
        //         loops: Animation.Infinite
        //         NumberAnimation {
        //             duration: 3000
        //             to: -150
        //             from: 150
        //             easing.type: Easing.InOutBounce
        //         }
        //         NumberAnimation {
        //             duration: 3000
        //             to: 150
        //             from: -150
        //             easing.type: Easing.OutQuad
        //         }
        //     }
        // }

        // Model {
        //     position: Qt.vector3d(0, 0, -50)
        //     source: "#Cube"
        //     scale: Qt.vector3d(1, 1, 1)
        //     materials: [
        //         PrincipledMaterial {
        //             baseColor: "blue"
        //         }
        //     ]
        // }

        FontMetrics {
            id: metrics
            font.pointSize: 64
        }

        Model {
            objectName: "spinningText"

            geometry: ExtrudedTextGeometry {
                id: gee
                depth: 20
                font.family: "Comic Sans MS"
                font.pointSize: 64
                scale: 64
                text: "Hello world"
                SequentialAnimation on depth {
                    loops: Animation.Infinite
                    NumberAnimation {
                        duration: 1000
                        to: 0
                        from: 100
                    }
                    NumberAnimation {
                        duration: 1000
                        to: 100
                        from: 0
                    }
                }
            }
            //position: Qt.vector3d(metrics.advanceWidth("Hello world") / -2, 0, gee.depth / -2)
            pivot: Qt.vector3d(metrics.advanceWidth("Hello world") / 2, 0, gee.depth / 2)
            materials: [
                PrincipledMaterial {
                    baseColor: "#ffffff"
                    roughness: .5
                }
            ]
            SequentialAnimation on eulerRotation.y {
                loops: Animation.Infinite
                NumberAnimation {
                    duration: 2000
                    from: 0
                    to: 360
                    easing.type: Easing.InElastic
                }
                NumberAnimation {
                    duration: 2000
                    from: 360
                    to: 0
                    easing.type: Easing.OutElastic
                }
            }
        }
    }
}
