#pragma once

#include <QDialog>

// Built-in help: quick start, what each control does, file system advice and troubleshooting.
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr);
};
