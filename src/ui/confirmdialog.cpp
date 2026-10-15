#include "confirmdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

ConfirmDialog::ConfirmDialog(const QString &title, const QString &headline, const ff::DiskInfo &d,
                             const QString &plan, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setMinimumWidth(560);

    auto *lay = new QVBoxLayout(this);

    auto *head = new QLabel(QStringLiteral("<b style='color:#b00020'>%1</b>").arg(headline.toHtmlEscaped()));
    head->setWordWrap(true);
    lay->addWidget(head);

    const QString what = QStringLiteral("<b>Disk %1</b> &ndash; %2<br>%3 &middot; %4%5")
                             .arg(d.number)
                             .arg(QString::fromStdString(d.displayName()).toHtmlEscaped())
                             .arg(QString::fromStdString(ff::humanSize(d.size)))
                             .arg(QString::fromLatin1(ff::busName(d.bus)))
                             .arg(d.letters.empty() ? QString()
                                                    : QStringLiteral(" &middot; ") +
                                                          QString::fromStdString(d.letters).toHtmlEscaped());
    auto *info = new QLabel(what);
    info->setTextFormat(Qt::RichText);
    lay->addWidget(info);

    auto *planBox = new QPlainTextEdit;
    planBox->setReadOnly(true);
    planBox->setPlainText(plan);
    planBox->setMinimumHeight(220);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    planBox->setFont(mono);
    lay->addWidget(planBox);

    auto *prompt = new QLabel(tr("To confirm, type the disk number <b>%1</b>:").arg(d.number));
    lay->addWidget(prompt);
    input_ = new QLineEdit;
    input_->setPlaceholderText(QString::number(d.number));
    input_->setMaximumWidth(120);
    lay->addWidget(input_);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    okBtn_ = box->addButton(tr("Erase and continue"), QDialogButtonBox::AcceptRole);
    okBtn_->setEnabled(false);
    box->button(QDialogButtonBox::Cancel)->setDefault(true);
    lay->addWidget(box);

    connect(input_, &QLineEdit::textChanged, this,
            [this, n = d.number](const QString &t) { okBtn_->setEnabled(t.trimmed() == QString::number(n)); });
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
