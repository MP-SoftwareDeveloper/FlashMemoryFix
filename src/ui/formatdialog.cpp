#include "formatdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr std::uint64_t kMiB = 1024ull * 1024ull;
}

FormatDialog::FormatDialog(const ff::DiskInfo &disk, QWidget *parent) : QDialog(parent), disk_(disk)
{
    setWindowTitle(tr("Clean + Partition + Format - Disk %1").arg(disk.number));
    setMinimumWidth(520);

    const std::uint64_t diskMB = disk.size / kMiB;

    auto *lay = new QVBoxLayout(this);
    auto *head = new QLabel(tr("<b>Disk %1</b> &ndash; %2 (%3)")
                                .arg(disk.number)
                                .arg(QString::fromStdString(disk.displayName()).toHtmlEscaped())
                                .arg(QString::fromStdString(ff::humanSize(disk.size))));
    lay->addWidget(head);

    auto *form = new QFormLayout;
    lay->addLayout(form);

    fs_ = new QComboBox;
    fs_->addItem(tr("FAT32 (max 32 GB with Windows)"), static_cast<int>(ff::Fs::Fat32));
    fs_->addItem(tr("exFAT"), static_cast<int>(ff::Fs::ExFat));
    fs_->addItem(tr("NTFS"), static_cast<int>(ff::Fs::Ntfs));
    // Sticks above 32 GB cannot get a full-size FAT32 from Windows: default to exFAT there.
    fs_->setCurrentIndex(diskMB > ff::kFat32MaxMB ? 1 : 0);
    form->addRow(tr("File system:"), fs_);

    style_ = new QComboBox;
    style_->addItem(tr("MBR (recommended for USB sticks and SD cards)"), static_cast<int>(ff::PartStyle::Mbr));
    style_->addItem(tr("GPT"), static_cast<int>(ff::PartStyle::Gpt));
    form->addRow(tr("Partition style:"), style_);

    label_ = new QLineEdit(QStringLiteral("USBDRIVE"));
    label_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z0-9 _-]*")), this));
    form->addRow(tr("Volume label:"), label_);

    useMax_ = new QCheckBox(tr("Use the whole disk"));
    useMax_->setChecked(true);
    form->addRow(tr("Partition size:"), useMax_);
    sizeMB_ = new QSpinBox;
    sizeMB_->setSuffix(tr(" MB"));
    sizeMB_->setRange(static_cast<int>(ff::kOtherMinMB), static_cast<int>(std::min<std::uint64_t>(diskMB, 2000000000ull)));
    sizeMB_->setValue(static_cast<int>(std::min<std::uint64_t>(diskMB, 8000)));
    form->addRow(QString(), sizeMB_);

    unit_ = new QComboBox;
    unit_->addItem(tr("Default"), 0);
    for (unsigned u : {4096u, 8192u, 16384u, 32768u, 65536u})
        unit_->addItem(tr("%1 KB").arg(u / 1024), u);
    form->addRow(tr("Allocation unit:"), unit_);

    quick_ = new QCheckBox(tr("Quick format"));
    quick_->setChecked(true);
    form->addRow(QString(), quick_);

    zero_ = new QCheckBox(tr("Also overwrite the whole disk with zeros (very slow)"));
    zero_->setToolTip(tr("Normally not needed. Existing partitions are erased automatically. Use this only if the "
                         "stick behaves strangely and you want every sector rewritten; it can take hours."));
    form->addRow(QString(), zero_);

    // Optional: put a file on the drive right after formatting (remembered between runs).
    QSettings settings;
    copy_ = new QCheckBox(tr("Copy a file to the drive after formatting"));
    copy_->setChecked(settings.value(QStringLiteral("copy/enabled"), false).toBool());
    form->addRow(tr("After formatting:"), copy_);
    auto *copyRow = new QHBoxLayout;
    copyPath_ = new QLineEdit(settings.value(QStringLiteral("copy/file")).toString());
    copyPath_->setPlaceholderText(tr("File to copy"));
    browse_ = new QPushButton(tr("Browse..."));
    copyRow->addWidget(copyPath_, 1);
    copyRow->addWidget(browse_);
    form->addRow(QString(), copyRow);
    verify_ = new QCheckBox(tr("Verify the copy (read it back and compare SHA-256)"));
    verify_->setChecked(true);
    form->addRow(QString(), verify_);

    hint_ = new QLabel;
    hint_->setWordWrap(true);
    hint_->setStyleSheet(QStringLiteral("color:#555;"));
    lay->addWidget(hint_);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(tr("Next..."));
    lay->addWidget(box);

    connect(fs_, &QComboBox::currentIndexChanged, this, &FormatDialog::updateState);
    connect(useMax_, &QCheckBox::toggled, this, &FormatDialog::updateState);
    connect(sizeMB_, &QSpinBox::valueChanged, this, &FormatDialog::updateState);
    connect(copy_, &QCheckBox::toggled, this, &FormatDialog::updateState);
    connect(browse_, &QPushButton::clicked, this, &FormatDialog::browse);
    connect(box, &QDialogButtonBox::accepted, this, &FormatDialog::tryAccept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateState();
}

ff::Fs FormatDialog::currentFs() const
{
    return static_cast<ff::Fs>(fs_->currentData().toInt());
}

void FormatDialog::updateState()
{
    const ff::Fs fs = currentFs();
    sizeMB_->setEnabled(!useMax_->isChecked());
    label_->setMaxLength(static_cast<int>(ff::maxLabelLength(fs)));
    copyPath_->setEnabled(copy_->isChecked());
    browse_->setEnabled(copy_->isChecked());
    verify_->setEnabled(copy_->isChecked());

    QStringList hints;
    const std::uint64_t diskMB = disk_.size / kMiB;
    const bool capApplies = useMax_->isChecked() || static_cast<std::uint64_t>(sizeMB_->value()) > ff::kFat32MaxMB;
    if (fs == ff::Fs::Fat32 && diskMB > ff::kFat32MaxMB && capApplies)
        hints << tr("Windows cannot format FAT32 above 32 GB: the partition is limited to %1 MB and the rest "
                    "of the disk stays unallocated. Choose exFAT to use all %2.")
                     .arg(ff::kFat32MaxMB)
                     .arg(QString::fromStdString(ff::humanSize(disk_.size)));
    hints << (disk_.partitions.empty() && disk_.layoutKnown
                  ? tr("The disk has no partitions: nothing needs wiping, a new partition is created directly.")
                  : tr("Existing partitions and their data will be erased automatically before the new partition is created."));
    if (disk_.bus == ff::BusType::Usb || disk_.bus == ff::BusType::Sd || disk_.bus == ff::BusType::Mmc)
        hints << tr("Windows shows only the first partition of a removable USB stick.");
    if (fs == ff::Fs::Fat32)
        hints << tr("The label is limited to 11 characters. FAT32 files must be smaller than 4 GiB.");
    hint_->setText(hints.join(QStringLiteral("\n")));
}

void FormatDialog::browse()
{
    const QString start = copyPath_->text().isEmpty() ? QString() : QFileInfo(copyPath_->text()).absolutePath();
    const QString f = QFileDialog::getOpenFileName(this, tr("File to copy to the drive"), start);
    if (!f.isEmpty())
        copyPath_->setText(f);
}

void FormatDialog::tryAccept()
{
    const std::string err = ff::checkLabel(currentFs(), label_->text().toStdString());
    if (!err.empty()) {
        QMessageBox::warning(this, tr("Invalid label"), QString::fromStdString(err));
        return;
    }
    if (copy_->isChecked()) {
        const QFileInfo fi(copyPath_->text());
        if (!fi.isFile()) {
            QMessageBox::warning(this, tr("File not found"), tr("Choose an existing file to copy, or untick the option."));
            return;
        }
        if (currentFs() == ff::Fs::Fat32 && fi.size() >= (4ll << 30)) {
            QMessageBox::warning(this, tr("File too large for FAT32"),
                                 tr("FAT32 cannot store files of 4 GiB or more. Choose exFAT or NTFS, or a smaller file."));
            return;
        }
        // Rough fit check against the partition that will be created.
        std::uint64_t partMB = useMax_->isChecked() ? disk_.size / kMiB : static_cast<std::uint64_t>(sizeMB_->value());
        if (currentFs() == ff::Fs::Fat32)
            partMB = std::min<std::uint64_t>(partMB, ff::kFat32MaxMB);
        if (static_cast<std::uint64_t>(fi.size()) > partMB * kMiB) {
            QMessageBox::warning(this, tr("File does not fit"),
                                 tr("The file is larger than the partition that will be created."));
            return;
        }
    }
    QSettings settings;
    settings.setValue(QStringLiteral("copy/enabled"), copy_->isChecked());
    if (!copyPath_->text().isEmpty())
        settings.setValue(QStringLiteral("copy/file"), copyPath_->text());
    accept();
}

QString FormatDialog::copyFile() const
{
    return copy_->isChecked() ? copyPath_->text() : QString();
}

bool FormatDialog::verifyCopy() const
{
    return verify_->isChecked();
}

ff::FormatOptions FormatDialog::options() const
{
    ff::FormatOptions o;
    o.fs = currentFs();
    o.style = static_cast<ff::PartStyle>(style_->currentData().toInt());
    o.label = label_->text().toStdString();
    o.sizeMB = useMax_->isChecked() ? 0 : static_cast<std::uint64_t>(sizeMB_->value());
    o.unitBytes = unit_->currentData().toUInt();
    o.quick = quick_->isChecked();
    o.zeroAll = zero_->isChecked();
    return o;
}
