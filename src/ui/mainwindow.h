#pragma once

#include <QMainWindow>
#include <QString>
#include <QThread>
#include <QTimer>
#include <vector>

#include "diskinfo.h"
#include "diskscript.h"

class DiskpartRunner;
class FileCopier;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(bool dryRun, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void refresh();
    void onSelectionChanged();
    void onClearReadOnly();
    void onFormat();
    void saveLog();
    void showHelp();
    void showAbout();
    void onRunnerFinished(bool ok);
    void onCopyFinished(bool ok, const QString &summary);
    void pollVolume();

private:
    // What to do after a successful format: wait for the new drive letter, then copy a file.
    struct PendingCopy {
        bool active = false;
        QString source;
        bool verify = true;
        ff::DiskInfo disk;  // the verified target; the copy only ever goes to a volume on this disk
        ff::Fs fs = ff::Fs::Fat32;
        std::string label;
        int attempts = 0;
    };

    void finishJob(bool ok, const QString &failureText);
    void startCopy(const QString &driveLetter);
    void buildUi();
    void populateTable(const QString &keepIdentity, int keepNumber);
    const ff::DiskInfo *selected() const;
    void updateDetails();
    void updateButtons();
    void updateBanner();
    void appendLog(const QString &text);
    // Re-reads the disks and makes sure the chosen device is still the same, modifiable device.
    bool revalidate(const ff::DiskInfo &old, ff::DiskInfo &fresh);
    bool checkTarget(const ff::DiskInfo &d);
    void startScript(const QString &title, const ff::Script &script);

    std::vector<ff::DiskInfo> disks_;
    std::vector<int> rowToDisk_;
    bool busy_ = false;
    bool elevated_ = true;
    QTimer autoRefresh_;
    QTimer volumeTimer_;
    PendingCopy pending_;
    QThread copyThread_;
    FileCopier *copier_ = nullptr;

    DiskpartRunner *runner_;
    QLabel *banner_ = nullptr;
    QPushButton *refreshBtn_ = nullptr;
    QCheckBox *showAll_ = nullptr;
    QTableWidget *table_ = nullptr;
    QPlainTextEdit *details_ = nullptr;
    QTableWidget *parts_ = nullptr;
    QPushButton *roBtn_ = nullptr;
    QPushButton *formatBtn_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QProgressBar *progress_ = nullptr;
};
