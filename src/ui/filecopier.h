#pragma once

#include <QObject>
#include <QString>

// Copies one file in 1 MiB chunks (meant to live on a worker thread), then checks the size
// and, optionally, that a SHA-256 of what was written equals the source's.
class FileCopier : public QObject
{
    Q_OBJECT
public:
    explicit FileCopier(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void start(const QString &source, const QString &destination, bool verify);

signals:
    void progress(qint64 done, qint64 total);
    void finished(bool ok, const QString &summary);
};
