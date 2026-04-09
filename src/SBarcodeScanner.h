#pragma once

#include <QObject>
#include <QVideoSink>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QRectF>
#include <QThread>
#include <atomic>

#include "SBarcodeDecoder.h"


class SBarcodeScanner : public QVideoSink, public QQmlParserStatus
{
    Q_OBJECT
    Q_PROPERTY(QString captured READ captured NOTIFY capturedChanged)
    Q_PROPERTY(QRectF captureRect READ captureRect WRITE setCaptureRect NOTIFY captureRectChanged)
    Q_PROPERTY(bool scanning READ scanning WRITE setScanning NOTIFY scanningChanged)
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY cameraAvailableChanged)
    Q_PROPERTY(QVideoSink* forwardVideoSink READ forwardVideoSink WRITE setForwardVideoSink NOTIFY forwardVideoSinkChanged)
 Q_INTERFACES(QQmlParserStatus)

public:
    explicit SBarcodeScanner(QObject* parent = nullptr);
    ~SBarcodeScanner();

    QString captured() const;

    QRectF captureRect() const;
    void setCaptureRect(const QRectF& rect);

    bool scanning() const { return m_scanning; }
    void setScanning(bool v);

    bool cameraAvailable() const;

    QVideoSink* forwardVideoSink() const { return m_forwardVideoSink; }
    void setForwardVideoSink(QVideoSink* sink);

signals:
    void capturedChanged(const QString&);
    void captureRectChanged(const QRectF&);
    void scanningChanged();
    void cameraAvailableChanged();
    void forwardVideoSinkChanged(QVideoSink*);

    void errorOccured(const QString&);

protected:
    void componentComplete() override;
    void classBegin() override;

private slots:
    void tryProcessFrame(const QVideoFrame& frame);
    void setCameraAvailable(bool available);
    void setCaptured(const QString&);

private:
    // Camera
    QCamera* makeDefaultCamera();
    void setCamera(QCamera* camera);

    // Preprocessing
    QImage preprocessFrame(const QVideoFrame& frame,
                           const QRect& crop) const;

private:
    QMediaCaptureSession m_capture;
    QCamera* m_camera = nullptr;

    SBarcodeDecoder m_decoder;
    QThread workerThread;

    QRectF m_captureRect {0.3, 0.3, 0.4, 0.4};

    QString m_captured;

    bool m_scanning = true;
    bool m_cameraAvailable = false;
    std::atomic<bool> m_frameProcessingInProgress{false};

    QVideoSink* m_forwardVideoSink = nullptr;
};
