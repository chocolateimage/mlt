#include "common.h"
#include <framework/mlt.h>
#include <stdlib.h>
#include <string.h>
#include <QAnimationDriver>
#include <QEasingCurve>
#include <QGraphicsBlurEffect>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsEffect>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QThread>

// ----- 3D ANIMATION -----

class D3Keyframe
{
public:
    double position;
    QVariant value;
    QEasingCurve curve;
};

class D3PropertyTrack
{
public:
    QString property;
    QList<D3Keyframe *> keyframes;
    ~D3PropertyTrack();
};

class D3ObjectTrack
{
public:
    QPointer<QObject> object;
    QList<D3PropertyTrack *> propertyTracks;
    ~D3ObjectTrack();
};

D3ObjectTrack::~D3ObjectTrack()
{
    qDeleteAll(propertyTracks);
}

D3PropertyTrack::~D3PropertyTrack()
{
    qDeleteAll(keyframes);
}

double lerp(double a, double b, double t)
{
    if (t == 0)
        return a;
    else if (t == 1)
        return b;
    else
        return a + t * (b - a);
}

QVariant lerpVariant(const QVariant &a, const QVariant &b, double t)
{
    Q_ASSERT(a.metaType().id() == b.metaType().id());

    switch (a.metaType().id()) {
    case QMetaType::Float: {
        return lerp(a.toFloat(), b.toFloat(), t);
    }
    case QMetaType::Double: {
        return lerp(a.toDouble(), b.toDouble(), t);
    }
    case QMetaType::Int: {
        return QVariant::fromValue((int) lerp(a.toInt(), b.toInt(), t));
    }
    case QMetaType::UInt: {
        return QVariant::fromValue((unsigned int) lerp(a.toUInt(), b.toUInt(), t));
    }
    case QMetaType::QVector2D: {
        QVector2D aV = a.value<QVector2D>();
        QVector2D bV = b.value<QVector2D>();
        return QVariant::fromValue(QVector2D(lerp(aV.x(), bV.x(), t), lerp(aV.y(), bV.y(), t)));
    }
    case QMetaType::QVector3D: {
        QVector3D aV = a.value<QVector3D>();
        QVector3D bV = b.value<QVector3D>();
        return QVariant::fromValue(
            QVector3D(lerp(aV.x(), bV.x(), t), lerp(aV.y(), bV.y(), t), lerp(aV.z(), bV.z(), t)));
    }
    case QMetaType::QColor: {
        QColor aV = a.value<QColor>();
        QColor bV = b.value<QColor>();
        // TODO: Find a better way to transition colors
        return QVariant::fromValue(QColor::fromRgbF(lerp(aV.redF(), bV.redF(), t),
                                                    lerp(aV.greenF(), bV.greenF(), t),
                                                    lerp(aV.blueF(), bV.blueF(), t),
                                                    lerp(aV.alphaF(), bV.alphaF(), t)));
    }
    case QMetaType::QString: {
        return t < 1 ? a.toString() : b.toString();
    }
    default: {
        qWarning() << "lerpVariant: Variant" << a.typeName() << "is not implemented";
        return a;
    }
    }
}

// ----------------

bool areWeGuiThread()
{
    qInfo() << "checking if we are in gui thread. current:" << QThread::currentThread()
            << "app:" << qApp->thread();
    return QThread::currentThread() == qApp->thread();
}

QVariant jsonToVariant(const QJsonObject &json)
{
    QString type = json["type"].toString();
    if (type == "nullptr" || type == "bool" || type == "int" || type == "uint" || type == "longlong"
        || type == "ulonglong" || type == "float" || type == "double" || type == "QString"
        || type == "QStringList" || type == "QUrl" || type == "QUuid") {
        return json["value"].toVariant();
    } else if (type == "QVector2D") {
        return QVariant::fromValue(QVector2D(json["x"].toDouble(), json["y"].toDouble()));
    } else if (type == "QVector3D") {
        return QVariant::fromValue(
            QVector3D(json["x"].toDouble(), json["y"].toDouble(), json["z"].toDouble()));
    } else if (type == "QColor") {
        return QVariant::fromValue(QColor(json["red"].toInt(),
                                          json["green"].toInt(),
                                          json["blue"].toInt(),
                                          json["alpha"].toInt()));
    } else {
        qWarning() << "jsonToVariant: Unknown variant type" << type;
        return QVariant::fromValue(nullptr);
    }
}

QObject *traverseQObjectByProperty(QObject *object, const QString &property)
{
    auto split = property.split(".");
    QObject *current = object;

    while (split.size() > 1) {
        QString name = split.takeFirst();
        current = current->findChild<QObject *>(name);
        if (current == nullptr)
            return current;
    }

    return current;
}

QString getPropNameFromProperty(const QString &property)
{
    return property.split(".").last();
}

class TitleState
{
public:
    bool created{false};
    QMutex prepareLock;
    bool prepared{false};
    int width{0};
    int height{0};
    QQuickRenderControl *renderControl{nullptr};
    QQuickWindow *window{nullptr};
    QQmlEngine *engine{nullptr};
    QOffscreenSurface *surface{nullptr};
    QOpenGLContext *context{nullptr};
    QOpenGLFramebufferObject *fbo{nullptr};
    QQuickItem *rootItem{nullptr};
    QString data = "";
    QList<D3ObjectTrack *> objectTracks;

    TitleState() {}

    void create()
    {
        if (created) {
            return;
        }

        auto toRun = [this]() {
            qInfo() << "we are now in main thread";
            this->_create();
            this->created = true;
        };

        if (areWeGuiThread()) {
            qInfo() << "we are already in main";
            toRun();
        } else {
            qInfo() << "asking main thread";
            QMetaObject::invokeMethod(qApp, toRun, Qt::BlockingQueuedConnection);
        }

        qInfo() << "getInstance OK2";
    }

    bool setSize(int width, int height)
    {
        bool changed = this->width != width || this->height != height;
        this->width = width;
        this->height = height;
        return changed;
    }

    void createAndLoadComponent()
    {
        QQmlComponent component(engine);
        component.setData(R"(import QtQuick3D
View3D {    environment: SceneEnvironment {
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.VeryHigh
    }
        Node {objectName: "viewNode"}})",
                          QUrl());

        rootItem = qobject_cast<QQuickItem *>(component.create());

        QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        QJsonObject obj = doc.object();
        QJsonArray objects = obj["objects"].toArray();

        for (auto jsonObjRef : objects) {
            QJsonObject jsonObj = jsonObjRef.toObject();
            QString qml = jsonObj["qml"].toString();
            QJsonObject propertiesMap = jsonObj["properties"].toObject();
            QVariantMap initialProperties;

            for (auto propertyKey : propertiesMap.keys()) {
                if (propertyKey.contains("."))
                    continue;
                QJsonObject propertyObj = propertiesMap[propertyKey].toObject();
                initialProperties[propertyKey] = jsonToVariant(propertyObj);
            }

            QQmlComponent component(engine);
            component.setData(qml.toUtf8(), QUrl());
            if (component.status() == QQmlComponent::Error) {
                qCritical() << "Error loading QML component" << jsonObj["objectType"].toString()
                            << component.errorString();
                continue;
            }
            QObject *newObject = component.createWithInitialProperties(initialProperties);

            for (auto propertyKey : propertiesMap.keys()) {
                if (!propertyKey.contains("."))
                    continue;

                QJsonObject propertyObj = propertiesMap[propertyKey].toObject();
                QObject *obj = traverseQObjectByProperty(newObject, propertyKey);
                QString propName = getPropNameFromProperty(propertyKey);
                obj->setProperty(qPrintable(propName), jsonToVariant(propertyObj));
            }

            QObject *view = rootItem->findChild<QObject *>("viewNode");
            newObject->setParent(view);
            newObject->setProperty("parent", QVariant::fromValue(view));

            if (jsonObj.contains("animationPropertyTracks")) {
                QJsonArray animationPropertyTracks = jsonObj["animationPropertyTracks"].toArray();
                if (!animationPropertyTracks.isEmpty()) {
                    D3ObjectTrack *objectTrack = new D3ObjectTrack();
                    objectTrack->object = newObject;

                    for (auto propertyTrackRef : animationPropertyTracks) {
                        QJsonObject propertyTrackJson = propertyTrackRef.toObject();
                        D3PropertyTrack *propertyTrack = new D3PropertyTrack();
                        propertyTrack->property = propertyTrackJson["property"].toString();

                        for (auto keyframeRef : propertyTrackJson["keyframes"].toArray()) {
                            auto keyframeJson = keyframeRef.toObject();
                            D3Keyframe *keyframe = new D3Keyframe();
                            keyframe->position = keyframeJson["position"].toDouble();
                            keyframe->value = jsonToVariant(keyframeJson["value"].toObject());
                            keyframe->curve = QEasingCurve(
                                static_cast<QEasingCurve::Type>(keyframeJson["curve"].toInt()));
                            propertyTrack->keyframes.append(keyframe);
                        }

                        objectTrack->propertyTracks.append(propertyTrack);
                    }

                    objectTracks.append(objectTrack);
                }
            }
        }
    }

    void animationUpdate(double current)
    {
        for (auto objectTrack : objectTracks) {
            if (objectTrack->object.isNull())
                continue;

            for (auto propertyTrack : objectTrack->propertyTracks) {
                if (propertyTrack->keyframes.isEmpty())
                    continue;

                int lastKeyframe = 0;
                for (int i = 0; i < propertyTrack->keyframes.size() - 1; i++) {
                    D3Keyframe *a = propertyTrack->keyframes[i];

                    if (current >= a->position) {
                        lastKeyframe = i;
                    }
                }

                D3Keyframe *a = propertyTrack->keyframes[lastKeyframe];
                D3Keyframe *b = a;
                double mappedTime = 0;
                if (propertyTrack->keyframes.size() >= 2) {
                    b = propertyTrack->keyframes[lastKeyframe + 1];
                    double keyframeLength = b->position - a->position;
                    mappedTime = (current - a->position) / keyframeLength;
                    mappedTime = std::clamp(mappedTime, 0.0, 1.0);
                }

                mappedTime = a->curve.valueForProgress(mappedTime);

                auto innerObject = traverseQObjectByProperty(objectTrack->object,
                                                             propertyTrack->property);
                QString innerProperty = getPropNameFromProperty(propertyTrack->property);

                QVariant lerped = lerpVariant(a->value, b->value, mappedTime);

                innerObject->setProperty(qPrintable(innerProperty), lerped);
            }
        }
    }

    void _create()
    {
        if (renderControl != nullptr)
            return;

        qInfo() << "create thread" << QThread::currentThread();
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setSwapBehavior(QSurfaceFormat::SingleBuffer);

        context = new QOpenGLContext();
        context->setFormat(format);
        context->create();

        surface = new QOffscreenSurface();
        surface->setFormat(context->format());
        surface->create();

        if (!surface->isValid()) {
            qInfo() << "surface NOT VALID!!!!!!!!!";
            return;
        }

        if (!context->makeCurrent(surface)) {
            qInfo() << "makeCurrent FAILED!!!!!!!!!";
            return;
        }

        fbo = new QOpenGLFramebufferObject(QSize(width, height),
                                           QOpenGLFramebufferObject::CombinedDepthStencil);

        renderControl = new QQuickRenderControl();
        window = new QQuickWindow(renderControl);
        window->setColor(Qt::transparent);
        window->setGeometry(0, 0, width, height);
        window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(context));
        window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(fbo->texture(), fbo->size()));
        engine = new QQmlEngine();
        createAndLoadComponent();
        rootItem->setSize(QSize(width, height));

        rootItem->setParentItem(window->contentItem());
        // component->setParent(renderControl->window()->contentItem());

        if (!renderControl->initialize()) {
            qInfo() << "renderControl FAILED!!!!!!!!!";
            return;
        }

        context->doneCurrent();
        qInfo() << "create()-d";
    }

    void use() { context->makeCurrent(surface); }

    void unUse() { context->doneCurrent(); }

    void destroy()
    {
        if (!created)
            return;

        auto toRun = [this]() {
            qInfo() << "DELELE: we are now in main thread";
            _destroy();
        };

        if (areWeGuiThread()) {
            qInfo() << "DELELE: we are already in main";
            toRun();
        } else {
            qInfo() << "DELELE: asking main thread";
            QMetaObject::invokeMethod(qApp, toRun, Qt::BlockingQueuedConnection);
        }
    }

    void _destroy()
    {
        if (renderControl == nullptr)
            return;
        qInfo() << "deleting title state instance";
        delete rootItem;
        delete engine;
        delete window;
        delete renderControl;
        delete fbo;
        delete context;
        delete surface;

        qDeleteAll(objectTracks);
        objectTracks.clear();

        renderControl = nullptr;
        created = false;
        data = "";
        qInfo() << "DELETED";
    }

    ~TitleState() { destroy(); }
};

static int producer_get_image(mlt_frame frame,
                              uint8_t **buffer,
                              mlt_image_format *format,
                              int *width,
                              int *height,
                              int writable)
{
    mlt_properties properties = MLT_FRAME_PROPERTIES(frame);
    mlt_producer producer = MLT_PRODUCER(mlt_properties_get_data(properties, "producer", NULL));
    mlt_properties producerProps = MLT_PRODUCER_PROPERTIES(producer);
    TitleState *state = (TitleState *) mlt_properties_get_data(producerProps, "state", nullptr);

    double fps = mlt_producer_get_fps(producer);
    double position = mlt_frame_original_position(frame);

    *format = mlt_image_rgba;
    int size = *width * *height * 4;
    *buffer = static_cast<uint8_t *>(mlt_pool_alloc(size));

    int smallWidth = mlt_properties_get_int(properties, "rescale_width");
    int smallHeight = mlt_properties_get_int(properties, "rescale_height");

    int fullWidth = mlt_properties_get_int(properties, "width");
    int fullHeight = mlt_properties_get_int(properties, "height");

    if (qApp->thread() != QThread::currentThread()) {
        state->prepareLock.lock();

        bool forceReload = false;
        if (mlt_properties_get_int(producerProps, "force_reload") > 0) {
            qInfo() << "FORCE RELOADING!!!!";
            forceReload = true;
            mlt_properties_set_int(producerProps, "force_reload", 0);
        }

        if (state->setSize(smallWidth, smallHeight) || forceReload) {
            state->destroy();
        }
        if (state->data == "") {
            state->data = mlt_properties_get(producerProps, "data");
            if (state->data == "") {
                state->prepareLock.unlock();
                return 0;
            }
        }

        state->create();
        state->prepareLock.unlock();
        QImage img2;
        QMetaObject::invokeMethod(
            qApp,
            [state, &img2, position, fps]() {
                state->use();
                state->animationUpdate(position / fps);
                state->renderControl->beginFrame();
                state->renderControl->polishItems();
                state->renderControl->sync();
                state->renderControl->render();
                state->renderControl->endFrame();
                img2 = state->fbo->toImage();
                state->unUse();
            },
            Qt::BlockingQueuedConnection);

        QImage img(*buffer, *width, *height, QImage::Format_RGBA8888);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        painter.drawImage(QRectF(0, 0, *width, *height), img2);
    }

    mlt_frame_set_image(frame, *buffer, size, mlt_pool_release);
    mlt_properties_set_int(producerProps, "width", *width);
    mlt_properties_set_int(producerProps, "height", *height);

    return 0;
}

static int producer_get_frame(mlt_producer producer, mlt_frame_ptr frame, int index)
{
    *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));
    if (frame != nullptr) {
        mlt_properties properties = MLT_FRAME_PROPERTIES(*frame);
        mlt_properties_set_data(properties, "producer", producer, 0, NULL, NULL);
        mlt_frame_set_position(*frame, mlt_producer_position(producer));
        mlt_frame_push_get_image(*frame, producer_get_image);
    }
    mlt_producer_prepare_next(producer);
    return 0;
}

static void producer_close(mlt_producer producer)
{
    fprintf(stderr, "--------- CLOSING PRODUCER KDENLIVE 3D SCENE ------------\n");
    mlt_properties producerProps = MLT_PRODUCER_PROPERTIES(producer);
    TitleState *state = (TitleState *) mlt_properties_get_data(producerProps, "state", nullptr);
    delete state;
    producer->close = NULL;
    mlt_producer_close(producer);
    free(producer);
}

extern "C" {

mlt_producer producer_kdenlive3dscene_init(mlt_profile profile,
                                           mlt_service_type type,
                                           const char *id,
                                           char *filename)
{
    /* Create a new producer object */
    fprintf(stderr, "--------- CREATING PRODUCER KDENLIVE 3D SCENE %s ------------\n", filename);

    mlt_producer producer = mlt_producer_new(profile);
    if (!createQApplicationIfNeeded(MLT_PRODUCER_SERVICE(producer))) {
        mlt_producer_close(producer);
        return NULL;
    }
    /* Get the properties interface */
    mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
    /* Callback registration */
    producer->get_frame = producer_get_frame;
    producer->close = (mlt_destructor) producer_close;
    mlt_properties_set(properties, "resource", filename);
    mlt_properties_set_int(properties, "seekable", 1);
    mlt_properties_set_int(properties, "meta.media.progressive", 1);
    TitleState *state = new TitleState();
    mlt_properties_set_data(properties, "state", state, 0, nullptr, nullptr);

    return producer;
}
}
