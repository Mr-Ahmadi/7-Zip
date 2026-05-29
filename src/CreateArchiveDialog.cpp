#include "CreateArchiveDialog.h"
#include "ArchiveService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QApplication>
#include <QStyle>
#include <QSettings>
#include <QProgressBar>

static const QStringList FORMATS = {"zip", "7z", "tar", "tar.gz", "tar.bz2", "tar.xz"};

CreateArchiveDialog::CreateArchiveDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Create Archive");
    setMinimumSize(540, 580);
    resize(580, 620);
    setAcceptDrops(true);
    buildUI();
}

void CreateArchiveDialog::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(18);
    root->setContentsMargins(24, 20, 24, 20);

    // ── Header ──
    auto *ti = new QLabel("Create Archive");
    QFont tf; tf.setPointSize(17); tf.setBold(true); ti->setFont(tf);
    root->addWidget(ti);
    auto *sub = new QLabel("Choose files and configure compression options");
    sub->setStyleSheet("color: rgba(128,128,128,0.55); font-size: 12px; margin-bottom: 2px;");
    root->addWidget(sub);

    // ── Files ──
    auto *fsec = new QLabel("FILES");
    fsec->setStyleSheet("font-size: 10px; font-weight: 700; color: rgba(128,128,128,0.45); letter-spacing: 1px; padding-left: 2px; margin-top: 4px;");
    root->addWidget(fsec);

    m_fileList = new QListWidget;
    m_fileList->setMinimumHeight(110);
    m_fileList->setAlternatingRowColors(true);
    m_fileList->setAcceptDrops(true);

    auto *fbr = new QHBoxLayout;
    fbr->setSpacing(8);
    auto *add = new QPushButton("+  Add Files");
    add->setCursor(Qt::PointingHandCursor);
    connect(add, &QPushButton::clicked, this, &CreateArchiveDialog::pickFiles);
    auto *addDir = new QPushButton("+  Add Folder");
    addDir->setCursor(Qt::PointingHandCursor);
    connect(addDir, &QPushButton::clicked, this, &CreateArchiveDialog::pickFolder);
    auto *rm = new QPushButton("Remove");
    rm->setCursor(Qt::PointingHandCursor);
    connect(rm, &QPushButton::clicked, this, &CreateArchiveDialog::removeFile);
    m_sizeLabel = new QLabel("No files chosen");
    m_sizeLabel->setStyleSheet("color: rgba(128,128,128,0.5); font-size: 11px;");
    fbr->addWidget(add); fbr->addWidget(addDir); fbr->addWidget(rm); fbr->addStretch(); fbr->addWidget(m_sizeLabel);
    root->addWidget(m_fileList);
    root->addLayout(fbr);

    // ── Options ──
    auto *osec = new QLabel("OPTIONS");
    osec->setStyleSheet("font-size: 10px; font-weight: 700; color: rgba(128,128,128,0.45); letter-spacing: 1px; padding-left: 2px; margin-top: 8px;");
    root->addWidget(osec);

    auto *og = new QFrame;
    og->setObjectName("optGroup");
    og->setStyleSheet(
        "#optGroup { background: rgba(128,128,128,0.04); border: 1px solid rgba(128,128,128,0.08);"
        " border-radius: 10px; padding: 6px; }");

    auto *of = new QFormLayout(og);
    of->setSpacing(14);
    of->setContentsMargins(18, 16, 18, 16);
    of->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    of->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    QFont lf = font(); lf.setPointSize(13);

    // Name
    m_nameEdit = new QLineEdit;
    m_nameEdit->setMinimumHeight(40);
    m_nameEdit->setPlaceholderText("MyArchive");
    connect(m_nameEdit, &QLineEdit::textChanged, this, &CreateArchiveDialog::updateBtn);
    auto *nr = new QHBoxLayout;
    nr->setSpacing(6);
    nr->addWidget(m_nameEdit, 1);
    auto *extLbl = new QLabel(".zip");
    extLbl->setStyleSheet("color: rgba(128,128,128,0.5); font-size: 13px;");
    nr->addWidget(extLbl);
    of->addRow("Archive Name:", nr);

    // Format
    m_fmtCombo = new QComboBox;
    m_fmtCombo->setMinimumHeight(40);
    m_fmtCombo->addItem("Zip (Compatible)", "zip");
    m_fmtCombo->addItem("7z (Best Compression)", "7z");
    m_fmtCombo->addItem("Tar (No Compression)", "tar");
    m_fmtCombo->addItem("Tar.gz", "tar.gz");
    m_fmtCombo->addItem("Tar.bz2", "tar.bz2");
    m_fmtCombo->addItem("Tar.xz", "tar.xz");
    connect(m_fmtCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CreateArchiveDialog::formatChanged);
    connect(m_fmtCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, extLbl]() { extLbl->setText("." + m_fmtCombo->currentData().toString()); });
    of->addRow("Format:", m_fmtCombo);

    // Compression level
    m_levelSpin = new QSpinBox;
    m_levelSpin->setMinimumHeight(40);
    m_levelSpin->setRange(0, 9);
    m_levelSpin->setValue(5);
    m_levelSpin->setFixedWidth(100);
    auto *li = new QLabel("0 = Store  \xC2\xB7  5 = Normal  \xC2\xB7  9 = Ultra");
    li->setStyleSheet("color: rgba(128,128,128,0.45); font-size: 11px; padding-left: 4px;");
    auto *lr = new QHBoxLayout;
    lr->setSpacing(4);
    lr->addWidget(m_levelSpin);
    lr->addWidget(li);
    lr->addStretch();
    of->addRow("Compression:", lr);
    root->addWidget(og);

    // ── Password ──
    auto *psec = new QLabel("PASSWORD (OPTIONAL)");
    psec->setStyleSheet("font-size: 10px; font-weight: 700; color: rgba(128,128,128,0.45); letter-spacing: 1px; padding-left: 2px; margin-top: 8px;");
    root->addWidget(psec);

    auto *pg = new QFrame;
    pg->setObjectName("pwdGroup");
    pg->setStyleSheet(
        "#pwdGroup { background: rgba(128,128,128,0.04); border: 1px solid rgba(128,128,128,0.08);"
        " border-radius: 10px; padding: 6px; }");

    auto *pf = new QFormLayout(pg);
    pf->setSpacing(14);
    pf->setContentsMargins(18, 16, 18, 16);
    pf->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pf->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_pwdChk = new QCheckBox("Protect with password");
    m_pwdChk->setStyleSheet("font-size: 12px;");
    connect(m_pwdChk, &QCheckBox::toggled, this, &CreateArchiveDialog::togglePassword);
    pf->addRow(m_pwdChk);

    m_pwdEdit = new QLineEdit;
    m_pwdEdit->setMinimumHeight(40);
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setPlaceholderText("Enter password");
    m_pwdEdit->setEnabled(false);
    connect(m_pwdEdit, &QLineEdit::textChanged, this, &CreateArchiveDialog::updateBtn);
    pf->addRow("Password:", m_pwdEdit);

    m_confirmEdit = new QLineEdit;
    m_confirmEdit->setMinimumHeight(40);
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setPlaceholderText("Confirm password");
    m_confirmEdit->setEnabled(false);
    connect(m_confirmEdit, &QLineEdit::textChanged, this, &CreateArchiveDialog::updateBtn);
    pf->addRow("Confirm:", m_confirmEdit);
    root->addWidget(pg);

    if (!ArchiveService::isBundledAvailable()) {
        auto *wn = new QLabel("\xE2\x9A\xA0  7za not bundled. Run: brew install p7zip");
        wn->setStyleSheet("color:#b85c00; font-size:11px; background:#fff3e0; border-radius:6px; padding:8px;");
        root->addWidget(wn);
    }

    // ── Buttons ──
    root->addStretch();
    auto *br = new QHBoxLayout;
    br->addStretch();
    br->setSpacing(10);
    auto *cancel = new QPushButton("Cancel");
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setFixedWidth(100);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_createBtn = new QPushButton("Create Archive");
    m_createBtn->setDefault(true);
    m_createBtn->setEnabled(false);
    m_createBtn->setCursor(Qt::PointingHandCursor);
    m_createBtn->setFixedWidth(140);
    connect(m_createBtn, &QPushButton::clicked, this, &CreateArchiveDialog::doCreate);
    br->addWidget(cancel); br->addWidget(m_createBtn);
    root->addLayout(br);
}

void CreateArchiveDialog::addFiles(const QStringList &paths)
{
    for (const auto &p : paths) {
        if (!m_files.contains(p)) {
            m_files.append(p);
            auto *item = new QListWidgetItem(QFileInfo(p).fileName());
            item->setToolTip(p);
            m_fileList->addItem(item);
        }
    }
    qint64 total = 0;
    for (const auto &f : m_files) total += QFileInfo(f).size();
    m_sizeLabel->setText(m_files.isEmpty() ? "No files chosen"
        : QString("%1 file(s), %2").arg(m_files.size()).arg(ArchiveService::formatSize(total)));
    updateBtn();
}

void CreateArchiveDialog::updateBtn()
{
    bool ok = !m_files.isEmpty() && !m_nameEdit->text().trimmed().isEmpty();
    if (m_pwdChk->isChecked())
        ok = ok && !m_pwdEdit->text().isEmpty() && m_pwdEdit->text() == m_confirmEdit->text();
    m_createBtn->setEnabled(ok);
}

void CreateArchiveDialog::pickFiles() { addFiles(QFileDialog::getOpenFileNames(this, "Select Files")); }

void CreateArchiveDialog::pickFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Folder",
        QFileInfo(m_files.isEmpty() ? QDir::homePath() : m_files.first()).absolutePath());
    if (!dir.isEmpty()) addFiles({dir});
}

void CreateArchiveDialog::removeFile()
{
    for (auto *it : m_fileList->selectedItems()) {
        int r = m_fileList->row(it); m_files.removeAt(r);
        delete m_fileList->takeItem(r);
    }
    qint64 total = 0;
    for (const auto &f : m_files) total += QFileInfo(f).size();
    m_sizeLabel->setText(m_files.isEmpty() ? "No files chosen"
        : QString("%1 file(s), %2").arg(m_files.size()).arg(ArchiveService::formatSize(total)));
    updateBtn();
}

void CreateArchiveDialog::formatChanged(int)
{
    updateBtn();
}

void CreateArchiveDialog::togglePassword(bool on)
{
    m_pwdEdit->setEnabled(on); m_confirmEdit->setEnabled(on);
    if (!on) { m_pwdEdit->clear(); m_confirmEdit->clear(); }
    updateBtn();
}

void CreateArchiveDialog::doCreate()
{
    QString name = m_nameEdit->text().trimmed();
    QString fmt = m_fmtCombo->currentData().toString();
    QString fn = name;
    if (!fn.endsWith("." + fmt)) fn += "." + fmt;

    QSettings s;
    QString lastDir = s.value("CreateArchive/lastDir", QDir::homePath()).toString();
    QString dest = QFileDialog::getSaveFileName(this, "Save Archive",
        lastDir + "/" + fn, QString("Archives (*.%1);;All Files (*)").arg(fmt));
    if (dest.isEmpty()) return;

    s.setValue("CreateArchive/lastDir", QFileInfo(dest).absolutePath());

    setEnabled(false);
    auto *svc = new ArchiveService(this);
    connect(svc, &ArchiveService::finished, this, [this, svc, dest](bool ok, const QString &) {
        svc->deleteLater(); setEnabled(true);
        if (ok) { emit archiveCreated(dest); accept(); }
    });
    connect(svc, &ArchiveService::errorOccurred, this, [this, svc](const QString &err) {
        svc->deleteLater(); setEnabled(true);
        QMessageBox::critical(this, "Error", err);
    });
    svc->createArchive(m_files, dest, fmt,
        m_pwdChk->isChecked() ? m_pwdEdit->text() : QString(), m_levelSpin->value());
}

void CreateArchiveDialog::dragEnterEvent(QDragEnterEvent *e)
{ if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void CreateArchiveDialog::dragMoveEvent(QDragMoveEvent *e)
{ if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void CreateArchiveDialog::dropEvent(QDropEvent *e)
{
    QStringList p;
    for (const auto &u : e->mimeData()->urls())
        if (u.isLocalFile()) p << u.toLocalFile();
    addFiles(p);
}
