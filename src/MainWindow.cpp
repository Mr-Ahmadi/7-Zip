#include "MainWindow.h"
#include "CreateArchiveDialog.h"
#include "ExtractDialog.h"

#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QTableView>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QHeaderView>
#include <QFileInfo>
#include <QPushButton>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QSettings>
#include <QDir>
#include <QCloseEvent>
#include <QTimer>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QGraphicsOpacityEffect>

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════
static QIcon makeFileIcon(const QString &ext)
{
    QPixmap pm(24, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    static const QMap<QString, QColor> colors{
        {"txt", "#5E9EFF"}, {"pdf", "#FF5E5E"},
        {"jpg", "#4CD964"}, {"jpeg","#4CD964"}, {"png", "#4CD964"}, {"gif", "#4CD964"},
        {"mp4", "#FF9500"}, {"mov", "#FF9500"},
        {"mp3", "#AF52DE"},
        {"zip", "#5AC8FA"}, {"7z", "#5AC8FA"}, {"rar", "#5AC8FA"},
        {"tar", "#8E8E93"}, {"gz", "#8E8E93"}, {"bz2", "#8E8E93"}, {"xz", "#8E8E93"},
    };
    QColor bg = colors.value(ext, "#C7C7CC");
    QPainterPath path;
    path.moveTo(4, 3); path.lineTo(15, 3); path.lineTo(21, 9);
    path.lineTo(21, 25); path.lineTo(4, 25); path.closeSubpath();
    QPainterPath corner;
    corner.moveTo(15, 3); corner.lineTo(15, 9); corner.lineTo(21, 9); corner.closeSubpath();
    p.fillPath(path, bg.lighter(140));
    p.fillPath(corner, bg);
    p.setPen(QPen(bg.darker(130), 1));
    p.drawPath(path);
    p.setPen(Qt::white);
    QFont f = p.font(); f.setPointSize(6); f.setBold(true); p.setFont(f);
    p.drawText(QRect(1, 7, 22, 14), Qt::AlignCenter, ext.left(3).toUpper());
    p.end();
    return QIcon(pm);
}

static QIcon archiveHeroIcon()
{
    QPixmap pm(80, 80);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor a = QApplication::palette().color(QPalette::Highlight);
    QPainterPath box; box.addRoundedRect(8, 14, 64, 54, 10, 10);
    p.fillPath(box, a);
    QPainterPath lid; lid.addRoundedRect(12, 4, 56, 18, 8, 8);
    p.fillPath(lid, a.lighter(130));
    p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(40, 34, 40, 60); p.drawLine(32, 52, 40, 60); p.drawLine(48, 52, 40, 60);
    p.end();
    return QIcon(pm);
}

static void openInSystem(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

static void revealInFinder(const QString &path)
{
    QProcess::startDetached("open", {"-R", path});
}

// ═══════════════════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════════════════
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_svc = new ArchiveService(this);
    m_lst = new ArchiveService(this);
    m_preview = new ArchiveService(this);
    connect(m_lst, &ArchiveService::contentsReady, this, &MainWindow::onContents);
    connect(m_lst, &ArchiveService::errorOccurred, this, &MainWindow::onError);
    connect(m_svc, &ArchiveService::finished, this, &MainWindow::onDone);
    connect(m_svc, &ArchiveService::errorOccurred, this, &MainWindow::onError);
    connect(m_svc, &ArchiveService::progressChanged, this, &MainWindow::onProgress);
    connect(m_preview, &ArchiveService::previewReady, this, &MainWindow::onPreviewReady);
    connect(m_preview, &ArchiveService::errorOccurred, this, [this](const QString &err) {
        statusBar()->showMessage("Preview failed: " + err, 5000);
        QMessageBox::warning(this, "Preview Error", err);
    });

    buildUI();
    buildMenu();
    buildToolbar();
    loadRecents();

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(120);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        m_filter->setFilterFixedString(m_search->text());
    });

    setWindowTitle("7-Zip");
    setMinimumSize(740, 520);
    resize(920, 640);
    setAcceptDrops(true);
    statusBar()->showMessage("Ready");

    showEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════
// UI builder
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::buildUI()
{
    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);
    buildEmptyPage();
    buildArchivePage();

    // Drop overlay
    m_dropOverlay = new QWidget(this);
    m_dropOverlay->setGraphicsEffect(new QGraphicsOpacityEffect);
    auto *dl = new QVBoxLayout(m_dropOverlay);
    dl->setAlignment(Qt::AlignCenter);
    auto *dt = new QLabel("Drop Archive to Open");
    QFont df; df.setPointSize(18); df.setBold(true); dt->setFont(df);
    dt->setAlignment(Qt::AlignCenter);
    dt->setStyleSheet("color: palette(highlight); background: transparent;");
    auto *ds = new QLabel("7z  \xC2\xB7  zip  \xC2\xB7  rar  \xC2\xB7  tar  \xC2\xB7  gz  \xC2\xB7  bz2  \xC2\xB7  xz");
    ds->setAlignment(Qt::AlignCenter);
    ds->setStyleSheet("color: rgba(128,128,128,0.5); background: transparent; font-size: 11px;");
    dl->addWidget(dt); dl->addWidget(ds);
    m_dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_dropOverlay->setStyleSheet("background: rgba(0,122,255,0.06); border: 2px dashed palette(highlight); border-radius: 14px;");
    m_dropOverlay->hide();
}

// ── Empty page ──────────────────────────────────────────────────────────
void MainWindow::buildEmptyPage()
{
    m_emptyPage = new QWidget;
    auto *lay = new QVBoxLayout(m_emptyPage);
    lay->setAlignment(Qt::AlignCenter);
    lay->setSpacing(8);

    auto *hero = new QLabel;
    hero->setPixmap(archiveHeroIcon().pixmap(72, 72));
    hero->setAlignment(Qt::AlignCenter);

    auto *title = new QLabel("7-Zip");
    QFont tf; tf.setPointSize(26); tf.setBold(true); title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);

    auto *sub = new QLabel("Open, extract, and create archives");
    sub->setProperty("subtitle", true);
    sub->setAlignment(Qt::AlignCenter);

    auto *openBtn = new QPushButton("Browse Archives...");
    openBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    openBtn->setFixedWidth(200); openBtn->setCursor(Qt::PointingHandCursor);
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openArchive);

    auto *createBtn = new QPushButton("New Archive...");
    createBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
    createBtn->setFixedWidth(200); createBtn->setCursor(Qt::PointingHandCursor);
    connect(createBtn, &QPushButton::clicked, this, &MainWindow::createArchive);

    // Button row
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    btnRow->addStretch();
    btnRow->addWidget(openBtn);
    btnRow->addWidget(createBtn);
    btnRow->addStretch();

    // Recent files
    auto *recentTitle = new QLabel("Recently Opened");
    recentTitle->setAlignment(Qt::AlignCenter);
    recentTitle->setStyleSheet("color: rgba(128,128,128,0.5); font-size: 11px; font-weight: 600; letter-spacing: 0.5px; margin-top: 8px;");

    m_recentsList = new QWidget;
    auto *rl = new QVBoxLayout(m_recentsList);
    rl->setAlignment(Qt::AlignCenter); rl->setSpacing(1);

    auto *dropHint = new QWidget;
    dropHint->setFixedWidth(340);
    dropHint->setStyleSheet("border: 1px dashed rgba(128,128,128,0.2); border-radius: 7px; background: transparent;");
    auto *hl = new QVBoxLayout(dropHint);
    auto *hlbl = new QLabel("or drop archives anywhere to open");
    hlbl->setAlignment(Qt::AlignCenter);
    hlbl->setStyleSheet("color: rgba(128,128,128,0.4); font-size: 11px; padding: 5px;");
    hl->addWidget(hlbl);

    lay->addStretch(3);
    lay->addWidget(hero, 0, Qt::AlignCenter);
    lay->addSpacing(4);
    lay->addWidget(title, 0, Qt::AlignCenter);
    lay->addSpacing(4);
    lay->addWidget(sub, 0, Qt::AlignCenter);
    lay->addSpacing(14);
    lay->addLayout(btnRow);
    lay->addSpacing(12);
    lay->addWidget(recentTitle, 0, Qt::AlignCenter);
    lay->addWidget(m_recentsList, 0, Qt::AlignCenter);
    lay->addWidget(dropHint, 0, Qt::AlignCenter);
    lay->addStretch(4);

    m_stack->addWidget(m_emptyPage);
}

// ── Archive page ────────────────────────────────────────────────────────
void MainWindow::buildArchivePage()
{
    m_archivePage = new QWidget;
    auto *ml = new QVBoxLayout(m_archivePage);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);

    // Info bar
    auto *bar = new QFrame;
    bar->setObjectName("infobar");
    bar->setStyleSheet("#infobar { background: palette(window); }");
    auto *bl = new QVBoxLayout(bar);
    bl->setContentsMargins(16, 8, 16, 6);
    bl->setSpacing(4);

    auto *topRow = new QHBoxLayout;
    m_infoLabel = new QLabel("Archive");
    QFont nf; nf.setPointSize(13); nf.setBold(true); m_infoLabel->setFont(nf);
    m_infoLabel->setMinimumWidth(100);

    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search files...");
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(180);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::searchChanged);

    topRow->addWidget(m_infoLabel, 1);
    topRow->addWidget(m_search, 0, Qt::AlignRight);
    bl->addLayout(topRow);

    // Progress bar — anchored to bottom of info bar
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFixedHeight(3);
    m_progress->setTextVisible(false);
    m_progress->hide();
    bl->addWidget(m_progress);

    ml->addWidget(bar);

    // Table
    m_model = new QStandardItemModel(0, 4, this);
    m_model->setHorizontalHeaderLabels({"Name", "Size", "Compressed", "Date"});

    m_filter = new QSortFilterProxyModel(this);
    m_filter->setSourceModel(m_model);
    m_filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filter->setFilterKeyColumn(0);

    m_table = new QTableView;
    m_table->setModel(m_filter);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->hide();
    m_table->setMouseTracking(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *hdr = m_table->horizontalHeader();
    hdr->setStretchLastSection(false);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    hdr->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    hdr->setMinimumSectionSize(70);

    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex &) {
        previewSelected();
    });
    connect(m_table, &QTableView::customContextMenuRequested, this, [this](const QPoint &pos) {
        contextMenu(pos);
    });

    ml->addWidget(m_table, 1);
    m_stack->addWidget(m_archivePage);
}

// ═══════════════════════════════════════════════════════════════════════════
// Menu
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::buildMenu()
{
    auto *file = menuBar()->addMenu("&File");
    auto *openAct = file->addAction(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton), "&Open Archive...");
    openAct->setShortcut(QKeySequence("Ctrl+O"));
    connect(openAct, &QAction::triggered, this, &MainWindow::openArchive);

    auto *newAct = file->addAction(QApplication::style()->standardIcon(QStyle::SP_FileIcon), "&New Archive...");
    newAct->setShortcut(QKeySequence("Ctrl+N"));
    connect(newAct, &QAction::triggered, this, &MainWindow::createArchive);

    file->addSeparator();
    m_closeAct = file->addAction("&Close Archive");
    m_closeAct->setShortcut(QKeySequence("Ctrl+W"));
    m_closeAct->setEnabled(false);
    connect(m_closeAct, &QAction::triggered, this, &MainWindow::closeArchive);

    file->addSeparator();
    auto *quit = file->addAction("&Quit");
    quit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    auto *archive = menuBar()->addMenu("&Archive");
    m_extractAct = archive->addAction(QApplication::style()->standardIcon(QStyle::SP_ArrowDown), "&Extract to Folder...");
    m_extractAct->setShortcut(QKeySequence("Ctrl+E"));
    m_extractAct->setEnabled(false);
    connect(m_extractAct, &QAction::triggered, this, &MainWindow::extractArchive);

    m_extractHereAct = archive->addAction("Extract &Here");
    m_extractHereAct->setShortcut(QKeySequence("Ctrl+D"));
    m_extractHereAct->setEnabled(false);
    connect(m_extractHereAct, &QAction::triggered, this, &MainWindow::extractHere);
}

// ═══════════════════════════════════════════════════════════════════════════
// Toolbar
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::buildToolbar()
{
    auto *tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setIconSize(QSize(16, 16));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto *open = tb->addAction(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton), "Open");
    open->setToolTip("Open Archive (Ctrl+O)");
    connect(open, &QAction::triggered, this, &MainWindow::openArchive);

    auto *createAct = tb->addAction(QApplication::style()->standardIcon(QStyle::SP_FileIcon), "New");
    createAct->setToolTip("New Archive (Ctrl+N)");
    connect(createAct, &QAction::triggered, this, &MainWindow::createArchive);

    tb->addSeparator();
    auto *extractTb = tb->addAction(QApplication::style()->standardIcon(QStyle::SP_ArrowDown), "Extract");
    extractTb->setToolTip("Extract to Folder... (Ctrl+E)");
    connect(extractTb, &QAction::triggered, this, &MainWindow::extractArchive);

    auto *hereTb = tb->addAction(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton), "Extract Here");
    hereTb->setToolTip("Extract Here (Ctrl+D)");
    connect(hereTb, &QAction::triggered, this, &MainWindow::extractHere);
}

// ═══════════════════════════════════════════════════════════════════════════
// State
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::showEmpty()
{
    m_stack->setCurrentWidget(m_emptyPage);
    m_path.clear(); m_entries.clear();
    m_model->removeRows(0, m_model->rowCount());
    if (m_extractAct) m_extractAct->setEnabled(false);
    if (m_extractHereAct) m_extractHereAct->setEnabled(false);
    if (m_closeAct) m_closeAct->setEnabled(false);
    m_search->clear();
    m_progress->hide();
    setWindowTitle("7-Zip");
    statusBar()->showMessage("Ready");
}

void MainWindow::showArchive(const QString &path)
{
    m_stack->setCurrentWidget(m_archivePage);
    if (m_extractAct) m_extractAct->setEnabled(true);
    if (m_extractHereAct) m_extractHereAct->setEnabled(true);
    if (m_closeAct) m_closeAct->setEnabled(true);
    m_path = path;
    setWindowTitle(QFileInfo(path).fileName() + " \xE2\x80\x94 7-Zip");
}

void MainWindow::refreshInfo()
{
    if (m_path.isEmpty()) return;
    QFileInfo fi(m_path);
    qint64 ts = 0, tc = 0;
    for (const auto &e : m_entries) { ts += e.size; tc += e.compressedSize; }
    QString ratio;
    if (ts > 0 && tc > 0) ratio = QString("  \xC2\xB7  %1%").arg(100 - 100 * tc / ts);
    m_infoLabel->setText(QString("%1  \xC2\xB7  %2 items  \xC2\xB7  %3%4")
        .arg(fi.fileName()).arg(m_entries.size())
        .arg(ArchiveService::formatSize(fi.size())).arg(ratio));
}

void MainWindow::loadArchive(const QString &path)
{
    if (!ArchiveService::isArchive(path)) {
        QMessageBox::warning(this, "Unsupported", "Unsupported: " + QFileInfo(path).suffix());
        return;
    }
    addRecent(path);
    statusBar()->showMessage("Loading\xE2\x80\xA6");
    m_model->removeRows(0, m_model->rowCount());
    showArchive(path);
    m_lst->listContents(path);
}

// ═══════════════════════════════════════════════════════════════════════════
// Recent files
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::addRecent(const QString &path)
{
    m_recents.removeAll(path);
    m_recents.prepend(path);
    if (m_recents.size() > 6) m_recents = m_recents.mid(0, 6);
    saveRecents();
    rebuildRecentWidget();
}

void MainWindow::rebuildRecentWidget()
{
    auto *rl = qobject_cast<QVBoxLayout *>(m_recentsList->layout());
    if (!rl) return;
    while (rl->count()) { auto *item = rl->takeAt(0); if (item->widget()) item->widget()->deleteLater(); delete item; }
    for (const auto &r : m_recents) {
        if (!QFileInfo::exists(r)) continue;
        auto *btn = new QPushButton(QFileInfo(r).fileName());
        btn->setFlat(true); btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(r);
        btn->setStyleSheet("QPushButton { text-align: left; padding: 2px 12px; border-radius: 4px; font-size: 11px; }"
                          "QPushButton:hover { background: palette(midlight); }");
        connect(btn, &QPushButton::clicked, this, [this, r]() { loadArchive(r); });
        rl->addWidget(btn);
    }
}

void MainWindow::saveRecents()
{
    QSettings s; s.beginGroup("MainWindow");
    s.setValue("recentFiles", m_recents);
    s.endGroup();
}

void MainWindow::loadRecents()
{
    QSettings s; s.beginGroup("MainWindow");
    m_recents = s.value("recentFiles").toStringList(); s.endGroup();
    m_recents.erase(std::remove_if(m_recents.begin(), m_recents.end(),
        [](const QString &p) { return !QFileInfo::exists(p); }), m_recents.end());
    rebuildRecentWidget();
}

// ═══════════════════════════════════════════════════════════════════════════
// Core actions
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::openArchive()
{
    QString p = QFileDialog::getOpenFileName(this, "Open Archive", {},
        "Archives (*.7z *.zip *.rar *.tar *.tar.gz *.tar.bz2 *.tar.xz"
        " *.tgz *.tbz2 *.txz *.gz *.bz2 *.xz);;All Files (*)");
    if (!p.isEmpty()) loadArchive(p);
}

void MainWindow::createArchive()
{
    auto *dlg = new CreateArchiveDialog(this);
    connect(dlg, &CreateArchiveDialog::archiveCreated, this, [this](const QString &p) { loadArchive(p); });
    dlg->open();
}

void MainWindow::extractArchive()
{
    if (m_path.isEmpty()) return;
    auto *dlg = new ExtractDialog(m_path, this);
    connect(dlg, &QDialog::accepted, this, [this]() { statusBar()->showMessage("Extraction complete.", 5000); });
    dlg->open();
}

void MainWindow::extractHere()
{
    if (m_path.isEmpty()) return;
    QFileInfo fi(m_path);
    QString dest = fi.absolutePath() + "/" + fi.completeBaseName();
    if (QDir(dest).exists()) dest = fi.absolutePath() + "/" + fi.completeBaseName() + "_extracted";

    statusBar()->showMessage("Extracting here\xE2\x80\xA6");
    m_progress->setValue(0); m_progress->show();
    m_opCount++;

    auto *svc = new ArchiveService(this);
    connect(svc, &ArchiveService::finished, this, [this, svc, dest](bool ok, const QString &msg) {
        svc->deleteLater(); m_opCount--;
        if (m_opCount <= 0) QTimer::singleShot(500, m_progress, &QProgressBar::hide);
        if (ok) {
            statusBar()->showMessage("Extracted to " + dest, 8000);
            QMessageBox::information(this, "Done", "Files extracted to:\n" + dest);
        }
    });
    connect(svc, &ArchiveService::errorOccurred, this, [this, svc](const QString &err) {
        svc->deleteLater(); m_opCount--;
        if (m_opCount <= 0) m_progress->hide();
        QMessageBox::critical(this, "Error", err);
    });
    connect(svc, &ArchiveService::progressChanged, this, [this](int pct, const QString &) { m_progress->setValue(pct); });
    svc->extractArchive(m_path, dest);
}

void MainWindow::closeArchive() { showEmpty(); statusBar()->showMessage("Closed.", 3000); }

// ═══════════════════════════════════════════════════════════════════════════
// Preview — async, non‑blocking
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::previewSelected()
{
    auto sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    QModelIndex src = m_filter->mapToSource(sel.first());
    int row = src.row();
    if (row < 0 || row >= m_entries.size()) return;
    const auto &entry = m_entries[row];
    if (entry.isFolder) return;

    statusBar()->showMessage("Opening: " + entry.name + "\xE2\x80\xA6");
    m_progress->setRange(0, 0); m_progress->show();

    m_preview->startPreview(m_path, entry.path);
}

void MainWindow::onPreviewReady(const QString &tempPath)
{
    m_progress->setRange(0, 100); m_progress->hide();
    // Copy to a stable path so the temp dir can be freed immediately
    QString name = QFileInfo(tempPath).fileName();
    QString safe = QDir::tempPath() + "/7zip_" + name;
    int n = 1;
    while (QFileInfo::exists(safe))
        safe = QDir::tempPath() + "/7zip_" + QString::number(n++) + "_" + name;
    QFile::remove(safe);
    if (QFile::copy(tempPath, safe)) {
        // Schedule cleanup after a short delay
        QTimer::singleShot(10000, this, [safe]() { QFile::remove(safe); });
        openInSystem(safe);
    } else {
        openInSystem(tempPath);
    }
    statusBar()->showMessage("Opened: " + name, 5000);
}

// ═══════════════════════════════════════════════════════════════════════════
// Context menu
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::contextMenu(const QPoint &pos)
{
    auto idx = m_table->indexAt(pos);
    if (!idx.isValid()) return;
    QModelIndex src = m_filter->mapToSource(idx);
    int row = src.row();
    if (row < 0 || row >= m_entries.size()) return;
    const auto &entry = m_entries[row];

    QMenu menu(this);

    if (!entry.isFolder) {
        auto *previewAct = menu.addAction("Open");
        previewAct->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        connect(previewAct, &QAction::triggered, this, &MainWindow::previewSelected);
    }

    auto *copyAct = menu.addAction("Copy Name");
    copyAct->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
    QString entryName = entry.name;
    connect(copyAct, &QAction::triggered, this, [entryName]() {
        QApplication::clipboard()->setText(entryName);
    });

    menu.addSeparator();

    auto *infoAct = menu.addAction("Properties");
    infoAct->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogInfoView));
    QString infoMsg = QString("Path: %1\nSize: %2\nCompressed: %3\nDate: %4\nType: %5")
        .arg(entry.path).arg(ArchiveService::formatSize(entry.size))
        .arg(ArchiveService::formatSize(entry.compressedSize))
        .arg(entry.date.toString("yyyy-MM-dd HH:mm"))
        .arg(entry.isFolder ? "Folder" : "File");
    connect(infoAct, &QAction::triggered, this, [this, entryName, infoMsg]() {
        QMessageBox::information(this, entryName, infoMsg);
    });

    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

// ═══════════════════════════════════════════════════════════════════════════
// Callbacks
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::onContents(const QVector<ArchiveEntry> &entries)
{
    m_entries = entries;
    m_model->removeRows(0, m_model->rowCount());

    for (const auto &e : entries) {
        int r = m_model->rowCount();
        m_model->insertRow(r);

        auto *ni = new QStandardItem(e.name);
        QString ext = e.isFolder ? "" : QFileInfo(e.name).suffix().toLower();
        ni->setIcon(e.isFolder
            ? QApplication::style()->standardIcon(QStyle::SP_DirIcon)
            : makeFileIcon(ext));
        ni->setToolTip(e.path);
        m_model->setItem(r, 0, ni);
        m_model->setItem(r, 1, new QStandardItem(e.size > 0 ? ArchiveService::formatSize(e.size) : "--"));
        m_model->setItem(r, 2, new QStandardItem(e.compressedSize > 0 ? ArchiveService::formatSize(e.compressedSize) : "--"));
        m_model->setItem(r, 3, new QStandardItem(e.date.toString("yyyy-MM-dd HH:mm")));
    }

    refreshInfo();
    statusBar()->showMessage(QString("Loaded %1 item(s)  \xC2\xB7  double-click to open").arg(entries.size()), 5000);
    m_search->clear();
    m_search->setFocus();
}

void MainWindow::onError(const QString &error)
{
    QMessageBox::critical(this, "Error", error);
    if (m_path.isEmpty()) showEmpty();
    statusBar()->showMessage("Error: " + error, 5000);
}

void MainWindow::onDone(bool ok, const QString &msg)
{
    if (ok) statusBar()->showMessage(msg, 5000);
    m_progress->setValue(100);
    QTimer::singleShot(500, m_progress, &QProgressBar::hide);
}

void MainWindow::onProgress(int pct, const QString &)
{
    m_progress->setValue(pct);
    m_progress->show();
}

void MainWindow::searchChanged(const QString &)
{
    m_searchTimer->start();
}

// ═══════════════════════════════════════════════════════════════════════════
// Drag & Drop
// ═══════════════════════════════════════════════════════════════════════════
void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
        m_dropOverlay->setGeometry(centralWidget()->rect());
        m_dropOverlay->raise(); m_dropOverlay->show();
    }
}
void MainWindow::dragMoveEvent(QDragMoveEvent *e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void MainWindow::dragLeaveEvent(QDragLeaveEvent *) { m_dropOverlay->hide(); }

void MainWindow::dropEvent(QDropEvent *e)
{
    m_dropOverlay->hide();
    if (!e->mimeData()->hasUrls()) return;
    for (const auto &url : e->mimeData()->urls())
        if (url.isLocalFile()) { loadArchive(url.toLocalFile()); return; }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveRecents();
    QMainWindow::closeEvent(e);
}
