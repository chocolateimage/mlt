import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

View3D {
    width: 800
    height: 600

    // external clock (ms)
    property real currentTime: 0

    environment: SceneEnvironment {
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.VeryHigh
    }

    PerspectiveCamera {
        position: Qt.vector3d(0, 200, 300)
        eulerRotation.x: -30
    }

    DirectionalLight {
        eulerRotation.x: (currentTime % 1000) / 1000 * 360
        eulerRotation.y: -70
        castsShadow: true

        color: Qt.rgba(156 / 255, 167 / 255, 1, 1.0)
        brightness: 1
        ambientColor: Qt.rgba(.2, .2, .2, 1)
    }

    DirectionalLight {
        eulerRotation.x: -180 + (currentTime % 1000) / 1000 * 360
        eulerRotation.y: -70
        castsShadow: true

        color: Qt.rgba(255 / 255, 134 / 255, 128 / 255, 1.0)
        brightness: 1
        ambientColor: Qt.rgba(.2, .2, .2, 1)
    }

    Model {
        position: Qt.vector3d(0, -200, 0)
        source: "#Cylinder"
        scale: Qt.vector3d(2, 0.2, 1)

        function easeOutQuad(x) {
            return 1 - (1 - x) * (1 - x);
        }

        function easeOutBounce(x) {
            const n1 = 7.5625;
            const d1 = 2.75;

            if (x < 1 / d1) {
                return n1 * x * x;
            } else if (x < 2 / d1) {
                x -= 1.5 / d1;
                return n1 * x * x + 0.75;
            } else if (x < 2.5 / d1) {
                x -= 2.25 / d1;
                return n1 * x * x + 0.9375;
            } else {
                x -= 2.625 / d1;
                return n1 * x * x + 0.984375;
            }
        }

        function easeInOutBounce(x) {
            if (x < 0.5)
                return (1 - easeOutBounce(1 - 2 * x)) / 2;
            else
                return (1 + easeOutBounce(2 * x - 1)) / 2;
        }

        y: {
            const t = currentTime % 6000;

            if (t < 3000) {
                const p = t / 3000;
                const e = easeInOutBounce(p);
                return 150 + (-300 * e);
            } else {
                const p = (t - 3000) / 3000;
                const e = easeOutQuad(p);
                return -150 + (300 * e);
            }
        }

        materials: [
            PrincipledMaterial {
                baseColor: "red"
            }
        ]
    }

    Model {
        position: Qt.vector3d(0, 0, -50)
        source: "#Cube"
        scale: Qt.vector3d(1, 1, 1)

        materials: [
            PrincipledMaterial {
                baseColor: "blue"
            }
        ]
    }

    FontMetrics {
        id: metrics
        font.pointSize: 64
    }

    Model {
        geometry: ExtrudedTextGeometry {
            id: gee
            depth: {
                var t = currentTime % 2000;
                if (t < 1000)
                    return 100 - (100 * (t / 1000));
                else
                    return 0 + (100 * ((t - 1000) / 1000));
            }

            font.family: "Comic Sans MS"
            font.pointSize: 64
            scale: 64
            text: "Hello world"
            asynchronous: false
        }

        pivot: Qt.vector3d(metrics.advanceWidth("Hello world") / 2, 0, gee.depth / 2)

        materials: [
            PrincipledMaterial {
                baseColor: "#ffffff"
                roughness: .5
            }
        ]

        eulerRotation.y: (currentTime % 2000) / 2000 * 360
    }
}
