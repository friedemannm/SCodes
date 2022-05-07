#ifndef SBARCODEWORKER_H
#define SBARCODEWORKER_H

#include <QObject>

class SBarCodeWorker : public QObject
{
    Q_OBJECT
public:
    explicit SBarCodeWorker(QObject *parent = nullptr);

signals:

};

#endif // SBARCODEWORKER_H
