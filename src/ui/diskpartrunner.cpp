#include "diskpartrunner.h"

#include <QDir>
#include <QTemporaryFile>

DiskpartRunner::DiskpartRunner(QObject *parent) : QObject(parent), proc_(new QProcess(this))
{
    proc_->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc_, &QProcess::readyRead, this, [this] {
        const QByteArray data = proc_->readAll();
        emit output(QString::fromLocal8Bit(data));
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            emit output(tr("ERROR: diskpart could not be started (run the program as administrator).\n"));
            finish(false);
        }
    });
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        emit output(tr("\ndiskpart exit code: %1\n").arg(code));
        finish(ok);
    });
}

DiskpartRunner::~DiskpartRunner()
{
    if (proc_->state() != QProcess::NotRunning) {
        proc_->kill();
        proc_->waitForFinished(2000);
    }
}

bool DiskpartRunner::dryRun() const
{
#ifdef Q_OS_WIN
    return dryRun_;
#else
    return true;
#endif
}

void DiskpartRunner::finish(bool ok)
{
    if (!running_)
        return;
    running_ = false;
    script_.reset();
    emit finished(ok);
}

void DiskpartRunner::run(const QString &title, const std::vector<std::string> &lines)
{
    if (running_)
        return;
    running_ = true;

    QString text;
    for (const auto &l : lines)
        text += QString::fromStdString(l) + QStringLiteral("\r\n");

    emit output(QStringLiteral("=== %1 ===\n").arg(title));
    emit output(tr("--- diskpart script ---\n"));
    QString shown = text;
    shown.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    emit output(shown);

    if (dryRun()) {
        emit output(tr("[dry run] nothing was executed.\n"));
        finish(true);
        return;
    }

    emit output(tr("--- diskpart output ---\n"));
    script_ = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/flashfix_XXXXXX.txt"));
    if (!script_->open()) {
        emit output(tr("ERROR: cannot create the temporary script file.\n"));
        finish(false);
        return;
    }
    script_->write(text.toLocal8Bit());
    script_->close();  // closes the handle only; the file is removed with the object

    const QString sysRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
    proc_->setProgram(sysRoot + QStringLiteral("\\System32\\diskpart.exe"));
    proc_->setArguments({QStringLiteral("/s"), QDir::toNativeSeparators(script_->fileName())});
    proc_->start();
}
