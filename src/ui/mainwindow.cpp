#include "mainwindow.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QFileInfo>
#include <QLocale>

#include "confirmdialog.h"
#include "diskpartrunner.h"
#include "filecopier.h"
#include "formatdialog.h"
#include "helpdialog.h"
#include "storage.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbt.h>
#endif

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString statusText(const ff::DiskInfo &d)
{
    if (d.isSystemDisk)
        return QObject::tr("System disk");
    if (d.offline)
        return QObject::tr("Offline");
    if (d.readOnly)
        return QObject::tr("Read-only");
    if (!d.layoutKnown)
        return QObject::tr("Partitions unreadable");
    if (d.style == ff::PartStyle::Raw)
        return QObject::tr("Not initialized");
    if (d.partitions.empty())
        return QObject::tr("No partitions");
    return QObject::tr("OK");
}

QTableWidgetItem *cell(const QString &text, bool dim = false, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter)
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    it->setTextAlignment(align);
    if (dim)
        it->setForeground(Qt::gray);
    return it;
}

}  // namespace

MainWindow::MainWindow(bool dryRun, QWidget *parent)
    : QMainWindow(parent), runner_(new DiskpartRunner(this))
{
    runner_->setDryRun(dryRun);
    elevated_ = ff::isElevated();
    buildUi();

    connect(runner_, &DiskpartRunner::output, this, &MainWindow::appendLog);
    connect(runner_, &DiskpartRunner::finished, this, &MainWindow::onRunnerFinished);

    autoRefresh_.setSingleShot(true);
    autoRefresh_.setInterval(1200);
    connect(&autoRefresh_, &QTimer::timeout, this, &MainWindow::refresh);

    // Waits for Windows to mount the freshly formatted volume.
    volumeTimer_.setSingleShot(true);
    volumeTimer_.setInterval(1500);
    connect(&volumeTimer_, &QTimer::timeout, this, &MainWindow::pollVolume);

    // File copies run on their own thread so the window stays responsive.
    copier_ = new FileCopier;
    copier_->moveToThread(&copyThread_);
    connect(&copyThread_, &QThread::finished, copier_, &QObject::deleteLater);
    connect(copier_, &FileCopier::progress, this, [this](qint64 done, qint64 total) {
        progress_->setRange(0, 100);
        progress_->setValue(total > 0 ? static_cast<int>(done * 100 / total) : 0);
    });
    connect(copier_, &FileCopier::finished, this, &MainWindow::onCopyFinished);
    copyThread_.start();

    appendLog(tr("Flash Memory Fix started.%1\n")
                  .arg(runner_->dryRun() ? tr(" DRY-RUN mode: scripts are shown, never executed.") : QString()));
    refresh();
}

MainWindow::~MainWindow()
{
    copyThread_.quit();
    copyThread_.wait(10000);
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Flash Memory Fix"));
    resize(1120, 780);

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);

    banner_ = new QLabel;
    banner_->setWordWrap(true);
    banner_->setStyleSheet(QStringLiteral("background:#fff3cd;color:#664d03;padding:6px;border-radius:4px;"));
    root->addWidget(banner_);

    auto *top = new QHBoxLayout;
    refreshBtn_ = new QPushButton(tr("Refresh"));
    showAll_ = new QCheckBox(tr("Also show internal disks (view only)"));
    auto *helpBtn = new QPushButton(tr("Help (F1)"));
    top->addWidget(refreshBtn_);
    top->addWidget(showAll_);
    top->addStretch();
    top->addWidget(helpBtn);
    root->addLayout(top);

    auto *split = new QSplitter(Qt::Vertical);
    root->addWidget(split, 1);

    // --- device list ---
    table_ = new QTableWidget(0, 7);
    table_->setHorizontalHeaderLabels({tr("Disk"), tr("Device"), tr("Size"), tr("Bus"), tr("Drive"), tr("Layout"), tr("Status")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->setMinimumHeight(130);
    // Light-green highlight for the selected device (also when the window is not focused).
    table_->setStyleSheet(QStringLiteral(
        "QTableWidget::item:selected, QTableWidget::item:selected:!active {"
        "  background-color: #b7efc5; color: #0b3d1a; }"));
    split->addWidget(table_);

    // --- details + partitions ---
    auto *mid = new QWidget;
    auto *midLay = new QHBoxLayout(mid);
    midLay->setContentsMargins(0, 0, 0, 0);
    details_ = new QPlainTextEdit;
    details_->setReadOnly(true);
    midLay->addWidget(details_, 1);
    parts_ = new QTableWidget(0, 6);
    parts_->setHorizontalHeaderLabels({tr("#"), tr("Offset"), tr("Size"), tr("Type"), tr("File system"), tr("Drive / label")});
    parts_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    parts_->setSelectionMode(QAbstractItemView::NoSelection);
    parts_->verticalHeader()->setVisible(false);
    parts_->horizontalHeader()->setStretchLastSection(true);
    midLay->addWidget(parts_, 1);
    split->addWidget(mid);

    // --- actions ---
    auto *actions = new QGroupBox(tr("Actions for the selected device"));
    auto *actLay = new QHBoxLayout(actions);
    roBtn_ = new QPushButton(tr("Clear read-only / bring online"));
    formatBtn_ = new QPushButton(tr("Clean + Partition + Format..."));
    roBtn_->setToolTip(tr("Removes the software write-protect flag and brings an offline disk online. Does not erase data."));
    formatBtn_->setToolTip(tr("Does everything in one step: erases old partitions when there are any, creates a new "
                              "partition and formats it with the file system you choose. Erases all data."));
    actLay->addWidget(roBtn_);
    actLay->addWidget(formatBtn_);
    actLay->addStretch();
    root->addWidget(actions);

    // --- log ---
    auto *logBox = new QGroupBox(tr("Log"));
    auto *logLay = new QVBoxLayout(logBox);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    log_->setFont(mono);
    log_->setMaximumBlockCount(5000);
    auto *saveBtn = new QPushButton(tr("Save log..."));
    logLay->addWidget(log_);
    auto *logBtns = new QHBoxLayout;
    logBtns->addStretch();
    logBtns->addWidget(saveBtn);
    logLay->addLayout(logBtns);
    split->addWidget(logBox);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 3);

    setCentralWidget(central);

    // Help menu
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *howTo = helpMenu->addAction(tr("&How to use"), this, &MainWindow::showHelp);
    howTo->setShortcut(QKeySequence::HelpContents);  // F1
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About Flash Memory Fix"), this, &MainWindow::showAbout);

    progress_ = new QProgressBar;
    progress_->setRange(0, 0);
    progress_->setMaximumWidth(160);
    progress_->hide();
    statusBar()->addPermanentWidget(progress_);

    connect(helpBtn, &QPushButton::clicked, this, &MainWindow::showHelp);
    connect(refreshBtn_, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(showAll_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);
    connect(roBtn_, &QPushButton::clicked, this, &MainWindow::onClearReadOnly);
    connect(formatBtn_, &QPushButton::clicked, this, &MainWindow::onFormat);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::saveLog);

    updateBanner();
    updateButtons();
}

void MainWindow::updateBanner()
{
    QStringList msgs;
    if (ff::isDemoBackend())
        msgs << tr("Demo data: this build is not running on Windows, so the disks shown are fake.");
    if (runner_->dryRun() && !ff::isDemoBackend())
        msgs << tr("Dry-run mode: scripts are only displayed, nothing is changed.");
    if (!elevated_)
        msgs << tr("Not running as administrator: partition tables cannot be read and cleaning / formatting is "
                   "disabled. Restart the program with \"Run as administrator\".");
    banner_->setText(msgs.join(QStringLiteral("  ")));
    banner_->setVisible(!msgs.isEmpty());
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_DEVICECHANGE &&
            (msg->wParam == DBT_DEVNODES_CHANGED || msg->wParam == DBT_DEVICEARRIVAL ||
             msg->wParam == DBT_DEVICEREMOVECOMPLETE))
            autoRefresh_.start();  // debounced: Windows sends several messages per plug event
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

// ---------------------------------------------------------------- list handling

void MainWindow::refresh()
{
    if (busy_)
        return;
    QString keepId;
    int keepNum = -1;
    if (const ff::DiskInfo *d = selected()) {
        keepId = qs(d->identity());
        keepNum = d->number;
    }
    disks_ = ff::enumerateDisks();
    populateTable(keepId, keepNum);
}

void MainWindow::populateTable(const QString &keepIdentity, int keepNumber)
{
    QSignalBlocker block(table_);
    table_->setRowCount(0);
    rowToDisk_.clear();

    int selectRow = -1;
    int firstTarget = -1;
    for (int i = 0; i < static_cast<int>(disks_.size()); ++i) {
        const ff::DiskInfo &d = disks_[static_cast<std::size_t>(i)];
        const bool target = d.isTarget();
        if (!target && !showAll_->isChecked())
            continue;
        const int row = table_->rowCount();
        table_->insertRow(row);
        rowToDisk_.push_back(i);
        const bool dim = !target;
        table_->setItem(row, 0, cell(QString::number(d.number), dim));
        table_->setItem(row, 1, cell(qs(d.displayName()), dim));
        table_->setItem(row, 2, cell(qs(ff::humanSize(d.size)), dim, Qt::AlignRight | Qt::AlignVCenter));
        table_->setItem(row, 3, cell(QString::fromLatin1(ff::busName(d.bus)), dim));
        table_->setItem(row, 4, cell(qs(d.letters), dim));
        QString layout = d.layoutKnown ? QString::fromLatin1(ff::styleName(d.style)) : tr("?");
        if (d.layoutKnown && d.style != ff::PartStyle::Raw)
            layout += tr(", %n partition(s)", nullptr, static_cast<int>(d.partitions.size()));
        table_->setItem(row, 5, cell(layout, dim));
        table_->setItem(row, 6, cell(statusText(d), dim));
        if (firstTarget < 0 && target)
            firstTarget = row;
        if (!keepIdentity.isEmpty() && qs(d.identity()) == keepIdentity && (selectRow < 0 || d.number == keepNumber))
            selectRow = row;
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    if (selectRow < 0)
        selectRow = firstTarget;
    if (selectRow >= 0)
        table_->selectRow(selectRow);
    block.unblock();
    onSelectionChanged();
}

const ff::DiskInfo *MainWindow::selected() const
{
    if (!table_)
        return nullptr;
    const auto rows = table_->selectionModel() ? table_->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.isEmpty())
        return nullptr;
    const int row = rows.first().row();
    if (row < 0 || row >= static_cast<int>(rowToDisk_.size()))
        return nullptr;
    return &disks_[static_cast<std::size_t>(rowToDisk_[static_cast<std::size_t>(row)])];
}

void MainWindow::onSelectionChanged()
{
    updateDetails();
    updateButtons();
}

void MainWindow::updateDetails()
{
    parts_->setRowCount(0);
    const ff::DiskInfo *d = selected();
    if (!d) {
        details_->setPlainText(tr("No device selected.\n\nPlug in a USB stick or SD card; the list updates automatically."));
        return;
    }

    QString t;
    t += tr("Disk %1: %2\n").arg(d->number).arg(qs(d->displayName()));
    t += tr("Bus: %1%2    Size: %3 (%4 bytes)\n")
             .arg(QString::fromLatin1(ff::busName(d->bus)))
             .arg(d->removableFlag ? tr(" (removable)") : QString())
             .arg(qs(ff::humanSize(d->size)))
             .arg(qs(ff::groupedBytes(d->size)));
    t += tr("Layout: %1    Write-protect flag: %2    State: %3\n")
             .arg(d->layoutKnown ? QString::fromLatin1(ff::styleName(d->style)) : tr("unreadable"))
             .arg(d->readOnly ? tr("READ-ONLY") : tr("no"))
             .arg(d->offline ? tr("offline") : tr("online"));
    if (!d->serial.empty() || !d->revision.empty())
        t += tr("Serial: %1    Revision: %2\n").arg(qs(d->serial), qs(d->revision));
    t += tr("Drive letters: %1\n").arg(d->letters.empty() ? tr("none") : qs(d->letters));
    t += tr("\nDiagnosis:\n");
    for (const auto &f : ff::diagnose(*d))
        t += QStringLiteral("  - ") + qs(f) + QLatin1Char('\n');
    const std::string why = d->refuseReason();
    if (!why.empty())
        t += tr("\nView only: %1\n").arg(qs(why));
    details_->setPlainText(t);

    for (const auto &p : d->partitions) {
        const int row = parts_->rowCount();
        parts_->insertRow(row);
        parts_->setItem(row, 0, cell(QString::number(p.number)));
        parts_->setItem(row, 1, cell(qs(ff::humanSize(p.offset)), false, Qt::AlignRight | Qt::AlignVCenter));
        QString size = qs(ff::humanSize(p.length));
        if (p.hasSpaceInfo)
            size += tr(" (%1 free)").arg(qs(ff::humanSize(p.freeBytes)));
        parts_->setItem(row, 2, cell(size, false, Qt::AlignRight | Qt::AlignVCenter));
        parts_->setItem(row, 3, cell(qs(p.type)));
        parts_->setItem(row, 4, cell(p.fileSystem.empty() ? tr("-") : qs(p.fileSystem)));
        QString dl = qs(p.letter);
        if (!p.label.empty())
            dl += (dl.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("\"") + qs(p.label) + QStringLiteral("\"");
        parts_->setItem(row, 5, cell(dl));
    }
    parts_->resizeColumnsToContents();
    parts_->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::updateButtons()
{
    const ff::DiskInfo *d = selected();
    const bool ok = d && d->isTarget() && elevated_ && !busy_;
    roBtn_->setEnabled(ok);
    formatBtn_->setEnabled(ok);
    refreshBtn_->setEnabled(!busy_);
    showAll_->setEnabled(!busy_);
}

// ---------------------------------------------------------------- safety helpers

bool MainWindow::checkTarget(const ff::DiskInfo &d)
{
    const std::string why = d.refuseReason();
    if (why.empty())
        return true;
    QMessageBox::warning(this, tr("Not allowed"), qs(why));
    return false;
}

bool MainWindow::revalidate(const ff::DiskInfo &old, ff::DiskInfo &fresh)
{
    const std::vector<ff::DiskInfo> now = ff::enumerateDisks();
    const ff::DiskInfo *p = ff::findSameDisk(old, now);
    if (!p) {
        QMessageBox::critical(this, tr("Device changed"),
                              tr("The selected device is gone or can no longer be identified. Nothing was changed. "
                                 "Refresh the list and try again."));
        return false;
    }
    fresh = *p;
    if (!checkTarget(fresh))
        return false;
    if (fresh.number != old.number)
        appendLog(tr("Note: Windows renumbered the device from disk %1 to disk %2; using disk %2.\n")
                      .arg(old.number)
                      .arg(fresh.number));
    return true;
}

// ---------------------------------------------------------------- actions

void MainWindow::onClearReadOnly()
{
    const ff::DiskInfo *sel = selected();
    if (!sel || !checkTarget(*sel))
        return;
    const ff::DiskInfo old = *sel;

    const auto r = QMessageBox::question(
        this, tr("Clear read-only"),
        tr("Clear the read-only flag and bring disk %1 (%2) online?\nNo data is erased.")
            .arg(old.number)
            .arg(qs(old.displayName())));
    if (r != QMessageBox::Yes)
        return;

    ff::DiskInfo fresh;
    if (!revalidate(old, fresh))
        return;
    startScript(tr("Clear read-only / bring online - disk %1").arg(fresh.number), ff::buildClearReadOnlyScript(fresh));
}

void MainWindow::onFormat()
{
    const ff::DiskInfo *sel = selected();
    if (!sel || !checkTarget(*sel))
        return;
    const ff::DiskInfo old = *sel;

    FormatDialog fd(old, this);
    if (fd.exec() != QDialog::Accepted)
        return;
    const ff::FormatOptions opts = fd.options();

    const ff::Script preview = ff::buildFormatScript(old, opts);
    if (!preview.ok) {
        QMessageBox::warning(this, tr("Cannot format"), qs(preview.error));
        return;
    }

    // Plain-language plan first, the exact diskpart script below it.
    QString plan = tr("These steps will run automatically:\n");
    int n = 1;
    for (const auto &st : preview.steps)
        plan += QStringLiteral("  %1. %2\n").arg(n++).arg(qs(st));
    if (!preview.notes.empty()) {
        plan += QLatin1Char('\n');
        for (const auto &note : preview.notes)
            plan += tr("Note: %1\n").arg(qs(note));
    }
    const QString copySource = fd.copyFile();
    if (!copySource.isEmpty()) {
        const qint64 bytes = QFileInfo(copySource).size();
        plan += tr("  %1. Wait until Windows has mounted the new drive\n").arg(n++);
        plan += tr("  %1. Copy \"%2\" (%3 bytes) to the root of the new drive\n")
                    .arg(n++)
                    .arg(QFileInfo(copySource).fileName())
                    .arg(QLocale(QLocale::English).toString(bytes));
        if (fd.verifyCopy())
            plan += tr("  %1. Read the copy back and compare its SHA-256 with the original\n").arg(n++);
    }
    plan += tr("\ndiskpart script:\n") + qs(ff::scriptToText(preview));

    ConfirmDialog dlg(tr("Clean + Partition + Format"),
                      tr("This will ERASE ALL DATA on the device below and create a new, empty file system."), old,
                      plan, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    ff::DiskInfo fresh;
    if (!revalidate(old, fresh))
        return;
    const ff::Script s = ff::buildFormatScript(fresh, opts);  // rebuilt from the freshly read state
    if (!s.ok) {
        QMessageBox::warning(this, tr("Cannot format"), qs(s.error));
        return;
    }
    pending_ = PendingCopy{};
    pending_.active = true;
    pending_.source = copySource;
    pending_.verify = fd.verifyCopy();
    pending_.disk = fresh;
    pending_.fs = opts.fs;
    pending_.label = opts.label;
    startScript(tr("Clean + Partition + Format - disk %1").arg(fresh.number), s);
}

void MainWindow::startScript(const QString &title, const ff::Script &script)
{
    if (!script.ok) {
        QMessageBox::warning(this, tr("Error"), qs(script.error));
        return;
    }
    busy_ = true;
    progress_->show();
    statusBar()->showMessage(title);
    updateButtons();
    runner_->run(title, script.lines);
}

void MainWindow::onRunnerFinished(bool ok)
{
    if (!ok) {
        finishJob(false, tr("diskpart reported an error. See the log for details.\n\nTip: unplug the device, "
                            "plug it back in (alone), and try again."));
        return;
    }
    if (!pending_.active) {  // e.g. "clear read-only": nothing to verify
        finishJob(true, QString());
        return;
    }
    if (runner_->dryRun()) {
        if (!pending_.source.isEmpty())
            appendLog(tr("[dry run] would now copy %1 to the new drive.\n").arg(pending_.source));
        finishJob(true, QString());
        return;
    }
    appendLog(tr("Waiting for Windows to mount the new volume...\n"));
    statusBar()->showMessage(tr("Waiting for the new drive..."));
    pending_.attempts = 0;
    volumeTimer_.start(2000);  // first look after a short pause; later polls use the 1.5 s interval
}

// Looks for the new volume on the disk we just formatted. Once it is there, copies the file (if any).
void MainWindow::pollVolume()
{
    if (!pending_.active)
        return;
    const std::vector<ff::DiskInfo> now = ff::enumerateDisks();
    const ff::DiskInfo *found = ff::findSameDisk(pending_.disk, now);

    const ff::PartitionInfo *good = nullptr;
    if (found) {
        const QString wantFs = QString::fromLatin1(ff::fsName(pending_.fs));
        for (const auto &p : found->partitions)
            if (!p.letter.empty() && qs(p.fileSystem).compare(wantFs, Qt::CaseInsensitive) == 0)
                good = &p;
    }

    if (good) {
        appendLog(tr("VERIFIED: %1 is %2, label \"%3\", %4 (%5 free).\n")
                      .arg(qs(good->letter))
                      .arg(qs(good->fileSystem))
                      .arg(qs(good->label))
                      .arg(qs(ff::humanSize(good->length)))
                      .arg(qs(ff::humanSize(good->freeBytes))));
        if (!pending_.source.isEmpty()) {
            startCopy(qs(good->letter));  // the letter comes from the disk we just formatted
            return;
        }
        finishJob(true, QString());
        return;
    }
    if (++pending_.attempts < 8) {
        volumeTimer_.start();
        return;
    }
    appendLog(tr("WARNING: the format reported success, but the new %1 volume did not show up with a drive letter.\n")
                  .arg(QString::fromLatin1(ff::fsName(pending_.fs))));
    finishJob(false, tr("The format finished, but Windows did not mount the new volume. Unplug the device, plug it "
                        "back in and check File Explorer."));
}

void MainWindow::startCopy(const QString &driveLetter)
{
    const QFileInfo src(pending_.source);
    // FAT32 cannot hold a file of 4 GiB or more; say so before writing anything.
    if (pending_.fs == ff::Fs::Fat32 && src.size() >= (4ll << 30)) {
        finishJob(false, tr("The file is 4 GiB or larger and cannot be stored on FAT32. The drive was formatted, "
                            "but nothing was copied."));
        return;
    }
    const QString dest = driveLetter + QStringLiteral("\\") + src.fileName();
    appendLog(tr("Copying %1 -> %2\n").arg(QDir::toNativeSeparators(src.absoluteFilePath()), dest));
    statusBar()->showMessage(tr("Copying %1...").arg(src.fileName()));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    QMetaObject::invokeMethod(copier_, "start", Qt::QueuedConnection, Q_ARG(QString, src.absoluteFilePath()),
                              Q_ARG(QString, dest), Q_ARG(bool, pending_.verify));
}

void MainWindow::onCopyFinished(bool ok, const QString &summary)
{
    appendLog((ok ? tr("COPY OK: ") : tr("COPY FAILED: ")) + summary + QLatin1Char('\n'));
    finishJob(ok, ok ? QString() : tr("The drive was formatted, but copying the file failed:\n\n%1").arg(summary));
}

// Common end of every job: reset state, report, refresh the list.
void MainWindow::finishJob(bool ok, const QString &failureText)
{
    busy_ = false;
    pending_ = PendingCopy{};
    volumeTimer_.stop();
    progress_->setRange(0, 0);
    progress_->hide();
    statusBar()->clearMessage();
    appendLog(ok ? tr("RESULT: OK\n\n") : tr("RESULT: FAILED - see the output above.\n\n"));
    updateButtons();
    if (!ok && !failureText.isEmpty())
        QMessageBox::warning(this, tr("Operation failed"), failureText);
    QTimer::singleShot(500, this, &MainWindow::refresh);
}

// ---------------------------------------------------------------- log

void MainWindow::appendLog(const QString &text)
{
    QString t = text;
    t.remove(QLatin1Char('\r'));
    QTextCursor c = log_->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(t);
    log_->setTextCursor(c);
    log_->ensureCursorVisible();
}

void MainWindow::showHelp()
{
    HelpDialog dlg(this);
    dlg.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About Flash Memory Fix"),
        tr("<b>Flash Memory Fix</b> %1<br><br>"
           "Finds USB sticks and SD cards and repairs them: clean, partition and format "
           "(FAT32, exFAT, NTFS).<br><br>"
           "Built with C++ and Qt %2. Devices are read with the Windows API; cleaning and formatting "
           "use diskpart.<br><br>Press F1 for help.")
            .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::saveLog()
{
    const QString name =
        QStringLiteral("flashfix_%1.txt").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, tr("Save log"), name, tr("Text files (*.txt)"));
    if (path.isEmpty())
        return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(log_->toPlainText().toUtf8()) < 0 || !f.commit())
        QMessageBox::warning(this, tr("Save log"), tr("Could not write %1.").arg(path));
}
