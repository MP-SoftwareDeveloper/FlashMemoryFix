#pragma once

#include <QDialog>

#include "diskinfo.h"
#include "diskscript.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// "Repair" dialog: file system, partition style, label, size, optional file to copy afterwards.
class FormatDialog : public QDialog
{
    Q_OBJECT
public:
    FormatDialog(const ff::DiskInfo &disk, QWidget *parent = nullptr);

    ff::FormatOptions options() const;
    // Empty when "copy a file" is not ticked.
    QString copyFile() const;
    bool verifyCopy() const;

private slots:
    void updateState();
    void browse();
    void tryAccept();

private:
    ff::Fs currentFs() const;

    ff::DiskInfo disk_;
    QComboBox *fs_;
    QComboBox *style_;
    QLineEdit *label_;
    QCheckBox *useMax_;
    QSpinBox *sizeMB_;
    QComboBox *unit_;
    QCheckBox *quick_;
    QCheckBox *zero_;
    QCheckBox *copy_;
    QLineEdit *copyPath_;
    QPushButton *browse_;
    QCheckBox *verify_;
    QLabel *hint_;
};
