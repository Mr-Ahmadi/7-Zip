#include "ExtractDialog.h"
#include "ArchiveService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QApplication>
#include <QStyle>
#include <QDir>
#include <QProgressBar>
#include <QSettings>

ExtractDialog::ExtractDialog(const QString &archivePath, QWidget *parent)
    : QDialog(parent), m_path(archivePath)
{
    QFileInfo fi(archivePath);
    m_name = fi.fileName();
    setWindowTitle("Extract: " + m_name);
    setMinimumSize(460, 280);
    resize(500, 300);

    // Default: Extract Here (same directory)
    QSettings s;
    QString last = s.value("Extract/lastDir").toString();
    QString suggested = last.isEmpty() ? fi.absolutePath() + "/" + fi.completeBaseName() : last;
    m_destEdit = new QLineEdit(suggested);
    buildUI();
}

void ExtractDialog::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(18);
    root->setContentsMargins(24, 20, 24, 20);

    // Header
    auto *ti = new QLabel("Extract Archive");
    QFont tf; tf.setPointSize(17); tf.setBold(true); ti->setFont(tf);
    root->addWidget(ti);

    // Archive info
    auto *ag = new QFrame;
    ag->setObjectName("infoCard");
    ag->setStyleSheet(
        "#infoCard { background: rgba(128,128,128,0.04);"
        " border: 1px solid rgba(128,128,128,0.08); border-radius: 10px; padding: 6px; }");
    auto *al = new QHBoxLayout(ag);
    al->setContentsMargins(14, 10, 14, 10);
    QFileInfo fi(m_path);
    auto *vc = new QVBoxLayout;
    vc->setSpacing(2);
    auto *nl = new QLabel(m_name);
    QFont nf; nf.setPointSize(13); nf.setBold(true); nl->setFont(nf);
    auto *sl = new QLabel(ArchiveService::formatSize(fi.size()));
    sl->setStyleSheet("color: rgba(128,128,128,0.5); font-size: 11px;");
    vc->addWidget(nl); vc->addWidget(sl);
    al->addLayout(vc); al->addStretch();
    root->addWidget(ag);

    // Destination
    auto *dsec = new QLabel("DESTINATION");
    dsec->setStyleSheet("font-size: 10px; font-weight: 700; color: rgba(128,128,128,0.45); letter-spacing: 1px; padding-left: 2px; margin-top: 8px;");
    root->addWidget(dsec);

    auto *dg = new QFrame;
    dg->setObjectName("destGroup");
    dg->setStyleSheet(
        "#destGroup { background: rgba(128,128,128,0.04); border: 1px solid rgba(128,128,128,0.08);"
        " border-radius: 10px; padding: 6px; }");
    auto *dl = new QVBoxLayout(dg);
    dl->setContentsMargins(16, 10, 16, 10);
    dl->setSpacing(8);
    auto *note = new QLabel("Files will be extracted to this folder");
    note->setStyleSheet("color: rgba(128,128,128,0.45); font-size: 10px; margin-bottom: 2px;");
    dl->addWidget(note);
    auto *dr = new QHBoxLayout;
    dr->setSpacing(8);
    dr->addWidget(m_destEdit, 1);
    auto *br = new QPushButton("Browse...");
    br->setCursor(Qt::PointingHandCursor);
    connect(br, &QPushButton::clicked, this, &ExtractDialog::browseDest);
    dr->addWidget(br);
    dl->addLayout(dr);
    root->addWidget(dg);

    // Password
    auto *psec = new QLabel("PASSWORD (OPTIONAL)");
    psec->setStyleSheet("font-size: 10px; font-weight: 700; color: rgba(128,128,128,0.45); letter-spacing: 1px; padding-left: 2px; margin-top: 8px;");
    root->addWidget(psec);

    auto *pg = new QFrame;
    pg->setObjectName("pwdGroup");
    pg->setStyleSheet(
        "#pwdGroup { background: rgba(128,128,128,0.04); border: 1px solid rgba(128,128,128,0.08);"
        " border-radius: 10px; padding: 6px; }");
    auto *pfl = new QFormLayout(pg);
    pfl->setContentsMargins(16, 10, 16, 10);
    pfl->setSpacing(10);
    pfl->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_pwdChk = new QCheckBox("Archive is encrypted");
    m_pwdChk->setStyleSheet("font-size: 12px;");
    connect(m_pwdChk, &QCheckBox::toggled, this, &ExtractDialog::togglePassword);
    pfl->addRow(m_pwdChk);
    m_pwdEdit = new QLineEdit;
    m_pwdEdit->setPlaceholderText("Enter password");
    m_pwdEdit->setEnabled(false);
    pfl->addRow("Password:", m_pwdEdit);
    root->addWidget(pg);

    root->addStretch();
    auto *bb = new QHBoxLayout; bb->addStretch(); bb->setSpacing(10);
    auto *cancel = new QPushButton("Cancel");
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setFixedWidth(100);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_extractBtn = new QPushButton("Extract");
    m_extractBtn->setDefault(true);
    m_extractBtn->setCursor(Qt::PointingHandCursor);
    m_extractBtn->setFixedWidth(120);
    connect(m_extractBtn, &QPushButton::clicked, this, &ExtractDialog::doExtract);
    bb->addWidget(cancel); bb->addWidget(m_extractBtn);
    root->addLayout(bb);
}

void ExtractDialog::browseDest()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Extract To", m_destEdit->text());
    if (!dir.isEmpty()) m_destEdit->setText(dir);
}

void ExtractDialog::togglePassword(bool on)
{
    m_pwdEdit->setEnabled(on);
    if (!on) m_pwdEdit->clear();
}

void ExtractDialog::doExtract()
{
    QString dest = m_destEdit->text().trimmed();
    if (dest.isEmpty()) { QMessageBox::warning(this, "Error", "Choose a destination."); return; }

    QSettings s;
    s.setValue("Extract/lastDir", dest);

    setEnabled(false);
    auto *svc = new ArchiveService(this);
    connect(svc, &ArchiveService::finished, this, [this, svc, dest](bool ok, const QString &) {
        svc->deleteLater(); setEnabled(true);
        if (ok) {
            QMessageBox::information(this, "Done", "Extracted to:\n" + dest);
            accept();
        }
    });
    connect(svc, &ArchiveService::errorOccurred, this, [this, svc](const QString &err) {
        svc->deleteLater(); setEnabled(true);
        QMessageBox::critical(this, "Error", err);
    });
    svc->extractArchive(m_path, dest, m_pwdChk->isChecked() ? m_pwdEdit->text() : QString());
}
