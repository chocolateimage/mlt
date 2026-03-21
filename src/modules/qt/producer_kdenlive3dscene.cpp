#include "common.h"
#include <cmath>
#include <framework/mlt.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <QAnimationDriver>
#include <QGraphicsBlurEffect>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsEffect>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QThread>
#include <qabstractanimation.h>
#include <qmutex.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qoffscreensurface.h>
#include <qopenglcontext.h>
#include <qopenglframebufferobject.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qsurfaceformat.h>
#include <qthread.h>

bool areWeGuiThread()
{
    qInfo() << "checking if we are in gui thread. current:" << QThread::currentThread()
            << "app:" << qApp->thread();
    return QThread::currentThread() == qApp->thread();
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
        QQmlComponent component = QQmlComponent(engine,
                                                "/home/lukas/Programming/mlt/Main.qml",
                                                QQmlComponent::PreferSynchronous);
        rootItem = qobject_cast<QQuickItem *>(component.create());
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

        renderControl = nullptr;
        created = false;
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
        if (state->setSize(smallWidth, smallHeight)) {
            state->destroy();
        }

        state->create();
        state->prepareLock.unlock();
        QImage img2;
        QMetaObject::invokeMethod(
            qApp,
            [state, &img2, position]() {
                state->use();
                state->rootItem->setProperty("currentTime", position / 60 * 1000);
                state->rootItem->setProperty("theText", "Example");
                state->context->makeCurrent(state->surface);
                state->renderControl->beginFrame();
                state->renderControl->polishItems();
                state->renderControl->sync();
                state->renderControl->render();
                state->renderControl->endFrame();
                img2 = state->fbo->toImage();
                state->unUse();
            },
            Qt::BlockingQueuedConnection);

        QImage img(*buffer, *width, *height, QImage::Format_ARGB32);
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
