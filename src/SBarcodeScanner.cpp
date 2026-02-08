#include "SBarcodeScanner.h"

#include <QMediaDevices>
#include <QImage>
#include <QtMath>

#include "private/debug.h"


// --------------------------------------------------
// Constructor / Destructor
// --------------------------------------------------

SBarcodeScanner::SBarcodeScanner(QObject* parent)
    : QVideoSink(parent)
{
    connect(this, &SBarcodeScanner::errorOccured, this,
            [](const QString& msg){
                qWarning() << "SCodes Error:" << msg;
            });

    m_capture.setVideoSink(this);

    connect(this, &QVideoSink::videoFrameChanged,
            this, &SBarcodeScanner::tryProcessFrame);


    m_decoder.moveToThread(&workerThread);

    connect(&m_decoder, &SBarcodeDecoder::capturedChanged,
            this, &SBarcodeScanner::setCaptured,
            Qt::QueuedConnection);

    connect(&m_decoder, &SBarcodeDecoder::errorOccured,
            this, &SBarcodeScanner::errorOccured,
            Qt::QueuedConnection);

    workerThread.start();
}


SBarcodeScanner::~SBarcodeScanner()
{
    workerThread.quit();
    workerThread.wait();
}



// --------------------------------------------------
// Qt Component Interface
// --------------------------------------------------

void SBarcodeScanner::componentComplete()
{
    if (!m_camera) {
        setCamera(makeDefaultCamera());
    }
}


void SBarcodeScanner::classBegin()
{
}



// --------------------------------------------------
// Public API
// --------------------------------------------------

QString SBarcodeScanner::captured() const
{
    return m_captured;
}


QRectF SBarcodeScanner::captureRect() const
{
    return m_captureRect;
}


void SBarcodeScanner::setCaptureRect(const QRectF& rect)
{
    if (rect == m_captureRect)
        return;

    m_captureRect = rect;

    emit captureRectChanged(m_captureRect);
}


void SBarcodeScanner::setScanning(bool v)
{
    if (m_scanning == v)
        return;

    m_scanning = v;

    emit scanningChanged();
}


bool SBarcodeScanner::cameraAvailable() const
{
    return m_cameraAvailable;
}


void SBarcodeScanner::setForwardVideoSink(QVideoSink* sink)
{
    if (m_forwardVideoSink == sink)
        return;

    if (m_forwardVideoSink) {
        disconnect(this, nullptr,
                   m_forwardVideoSink, nullptr);
    }

    if (sink) {
        connect(this, &QVideoSink::videoFrameChanged,
                sink, &QVideoSink::setVideoFrame);
    }

    m_forwardVideoSink = sink;

    emit forwardVideoSinkChanged(m_forwardVideoSink);
}



// --------------------------------------------------
// Frame Processing
// --------------------------------------------------

void SBarcodeScanner::tryProcessFrame(const QVideoFrame& frame)
{
    if (!m_scanning || m_frameProcessingInProgress)
        return;

    if (!m_camera)
        return;


    m_frameProcessingInProgress = true;


    // Compute crop rectangle
    const QSize res = m_camera->cameraFormat().resolution();

    QRect crop = QRectF(
                     m_captureRect.x() * res.width(),
                     m_captureRect.y() * res.height(),
                     m_captureRect.width() * res.width(),
                     m_captureRect.height() * res.height()
                     ).toRect();


    // Preprocess in UI thread (fast enough for mobile)
    QImage preprocessed =
        preprocessFrame(frame, crop);


    if (preprocessed.isNull()) {
        m_frameProcessingInProgress = false;
        return;
    }


    // Send to worker
    QMetaObject::invokeMethod(
        &m_decoder,
        [this, preprocessed]() {

            m_decoder.process(
                preprocessed,
                SCodes::toZXingFormat(
                    SCodes::SBarcodeFormat::QRCode
                    )
                );

            m_frameProcessingInProgress = false;
        },
        Qt::QueuedConnection
        );
}



// --------------------------------------------------
// Preprocessing (Qt-only, Mobile optimized)
// --------------------------------------------------

QImage SBarcodeScanner::preprocessFrame(const QVideoFrame& frame,
                                        const QRect& crop) const
{
    QVideoFrame copy(frame);

    if (!copy.map(QVideoFrame::ReadOnly))
        return {};


    QImage img = copy.toImage();

    copy.unmap();


    if (img.isNull())
        return {};


    // 1. Crop
    QImage cropped = img.copy(crop);


    // 2. Downscale (main improvement)
    QImage scaled = cropped.scaled(
        cropped.width() * 0.5,
        cropped.height() * 0.5,
        Qt::IgnoreAspectRatio,
        Qt::FastTransformation
        );


    // 3. Grayscale
    QImage gray =
        scaled.convertToFormat(QImage::Format_Grayscale8);


    // 4. Contrast stretch
    uchar min = 255;
    uchar max = 0;

    for (int y = 0; y < gray.height(); ++y) {

        const uchar* line =
            gray.constScanLine(y);

        for (int x = 0; x < gray.width(); ++x) {

            uchar v = line[x];

            min = qMin(min, v);
            max = qMax(max, v);
        }
    }


    int diff = qMax(1, int(max - min));
    float scale = 255.0f / diff;


    for (int y = 0; y < gray.height(); ++y) {

        uchar* line = gray.scanLine(y);

        for (int x = 0; x < gray.width(); ++x) {

            line[x] =
                uchar((line[x] - min) * scale);
        }
    }

    return gray;
}



// --------------------------------------------------
// Camera Handling
// --------------------------------------------------

QCamera* SBarcodeScanner::makeDefaultCamera()
{
    auto defaultCamera =
        QMediaDevices::defaultVideoInput();

    if (defaultCamera.isNull()) {
        errorOccured("No default camera found");
        return nullptr;
    }


    auto camera =
        new QCamera(defaultCamera, this);


    if (camera->error()) {
        errorOccured(
            "Camera init error: " +
            camera->errorString()
            );
        return nullptr;
    }


    auto formats =
        camera->cameraDevice().videoFormats();


    if (formats.empty()) {
        errorOccured("No camera formats");
        return nullptr;
    }


    // Prefer medium resolution (mobile friendly)
    std::sort(
        formats.begin(),
        formats.end(),
        [](const auto& a, const auto& b) {
            return a.resolution().width()
            * a.resolution().height()
                < b.resolution().width()
                      * b.resolution().height();
        }
        );


    // Pick ~60% percentile (not max)
    int idx = int(formats.size() * 0.6);
    idx = qBound(0, idx, formats.size() - 1);

    camera->setCameraFormat(formats[idx]);

    camera->setFocusMode(QCamera::FocusModeAuto);

    return camera;
}


void SBarcodeScanner::setCamera(QCamera* camera)
{
    if (m_camera == camera)
        return;


    if (m_camera) {

        m_camera->stop();

        m_capture.setCamera(nullptr);

        if (m_camera->parent() == this)
            delete m_camera;

        m_camera = nullptr;
    }


    if (camera) {

        connect(camera, &QCamera::errorOccurred,
                this,
                [this](auto, const QString& s) {
                    errorOccured("Camera: " + s);
                });

        m_decoder.setResolution(
            camera->cameraFormat().resolution()
            );

        m_capture.setCamera(camera);

        m_camera = camera;

        m_camera->start();
    }


    setCameraAvailable(m_camera != nullptr);
}



// --------------------------------------------------
// Internal Slots
// --------------------------------------------------

void SBarcodeScanner::setCameraAvailable(bool available)
{
    if (m_cameraAvailable == available)
        return;

    m_cameraAvailable = available;

    emit cameraAvailableChanged();
}


void SBarcodeScanner::setCaptured(const QString& v)
{
    m_captured = v;

    emit capturedChanged(m_captured);
}
