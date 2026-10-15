#include "filecopier.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#endif

namespace {

QString bytesText(qint64 n)
{
    return QLocale(QLocale::English).toString(n);
}

}  // namespace

void FileCopier::start(const QString &source, const QString &destination, bool verify)
{
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        emit finished(false, tr("Cannot open the source file: %1").arg(in.errorString()));
        return;
    }
    QFile out(destination);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit finished(false, tr("Cannot create %1: %2").arg(destination, out.errorString()));
        return;
    }

    auto fail = [&](const QString &msg) {
        out.close();
        QFile::remove(destination);  // never leave a partial file behind
        emit finished(false, msg);
    };

    const qint64 total = in.size();
    qint64 done = 0;
    QCryptographicHash srcHash(QCryptographicHash::Sha256);
    QByteArray buf(1 << 20, Qt::Uninitialized);

    while (!in.atEnd()) {
        const qint64 n = in.read(buf.data(), buf.size());
        if (n < 0) {
            fail(tr("Read error: %1").arg(in.errorString()));
            return;
        }
        srcHash.addData(QByteArrayView(buf.constData(), n));
        if (out.write(buf.constData(), n) != n) {
            fail(tr("Write error (is the drive full or write-protected?): %1").arg(out.errorString()));
            return;
        }
        done += n;
        emit progress(done, total);
    }

    if (!out.flush()) {
        fail(tr("Flush error: %1").arg(out.errorString()));
        return;
    }
#ifdef Q_OS_WIN
    // Push the data through the OS cache to the device before we report success.
    FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(out.handle())));
#endif
    out.close();

    const qint64 written = QFileInfo(destination).size();
    if (written != total) {
        fail(tr("Size mismatch: source %1 bytes, copy %2 bytes.").arg(bytesText(total), bytesText(written)));
        return;
    }

    QString hashText = tr("not verified");
    if (verify) {
        QFile chk(destination);
        if (!chk.open(QIODevice::ReadOnly)) {
            fail(tr("Cannot read the copy back: %1").arg(chk.errorString()));
            return;
        }
        QCryptographicHash dstHash(QCryptographicHash::Sha256);
        while (!chk.atEnd()) {
            const qint64 n = chk.read(buf.data(), buf.size());
            if (n < 0) {
                chk.close();
                fail(tr("Read-back error: %1").arg(chk.errorString()));
                return;
            }
            dstHash.addData(QByteArrayView(buf.constData(), n));
        }
        chk.close();
        if (srcHash.result() != dstHash.result()) {
            fail(tr("Checksum mismatch: the copy on the drive differs from the source (SHA-256 %1 vs %2).")
                     .arg(QString::fromLatin1(srcHash.result().toHex().left(16)),
                          QString::fromLatin1(dstHash.result().toHex().left(16))));
            return;
        }
        hashText = tr("SHA-256 verified (%1...)").arg(QString::fromLatin1(srcHash.result().toHex().left(16)));
    }

    emit finished(true, tr("Copied %1 bytes to %2 - %3").arg(bytesText(total), QDir::toNativeSeparators(destination), hashText));
}
