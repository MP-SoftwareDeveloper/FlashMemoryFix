#pragma once

#include <QDialog>

#include "diskinfo.h"

class QLineEdit;
class QPushButton;

// "Type the disk number to confirm" dialog shown before anything destructive.
class ConfirmDialog : public QDialog
{
    Q_OBJECT
public:
    // `plan` is shown verbatim (the steps that will run + the diskpart script).
    ConfirmDialog(const QString &title, const QString &headline, const ff::DiskInfo &disk, const QString &plan,
                  QWidget *parent = nullptr);

private:
    QLineEdit *input_;
    QPushButton *okBtn_;
};
