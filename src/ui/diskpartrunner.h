#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <memory>
#include <string>
#include <vector>

class QTemporaryFile;

// Runs "diskpart /s <script>" asynchronously and streams its output.
// In dry-run mode (always on non-Windows) the script is only printed.
class DiskpartRunner : public QObject
{
    Q_OBJECT
public:
    explicit DiskpartRunner(QObject *parent = nullptr);
    ~DiskpartRunner() override;

    void setDryRun(bool on) { dryRun_ = on; }
    bool dryRun() const;
    bool isRunning() const { return running_; }

    void run(const QString &title, const std::vector<std::string> &lines);

signals:
    void output(const QString &text);  // chunks, already newline-terminated
    void finished(bool ok);

private:
    void finish(bool ok);

    QProcess *proc_;
    std::unique_ptr<QTemporaryFile> script_;
    bool dryRun_ = false;
    bool running_ = false;
};
