#pragma once
#include <QMainWindow>
#include <QVector>
#include <QStringList>
#include "ArchiveService.h"

class QTableView;
class QStandardItemModel;
class QSortFilterProxyModel;
class QStackedWidget;
class QLabel;
class QLineEdit;
class QAction;
class QWidget;
class QProgressBar;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openArchive();
    void createArchive();
    void extractArchive();
    void extractHere();
    void closeArchive();
    void previewSelected();
    void onPreviewReady(const QString &tempPath);
    void contextMenu(const QPoint &pos);
    void onContents(const QVector<ArchiveEntry> &entries);
    void onError(const QString &error);
    void onDone(bool ok, const QString &msg);
    void onProgress(int pct, const QString &status);
    void searchChanged(const QString &text);

public:
    // Called from main.cpp for command-line / "Open With"
    void loadArchive(const QString &path);

private:
    void buildUI();
    void buildMenu();
    void buildToolbar();
    void buildEmptyPage();
    void buildArchivePage();
    void showEmpty();
    void showArchive(const QString &path);
    void refreshInfo();

    void addRecent(const QString &path);
    void clearRecents();
    void rebuildRecentWidget();
    void saveRecents();
    void loadRecents();

    // Pages
    QStackedWidget *m_stack = nullptr;
    QWidget *m_emptyPage = nullptr;
    QWidget *m_archivePage = nullptr;
    QWidget *m_dropOverlay = nullptr;

    // Empty page
    QWidget *m_recentsList = nullptr;

    // Archive page
    QTableView *m_table = nullptr;
    QStandardItemModel *m_model = nullptr;
    QSortFilterProxyModel *m_filter = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLineEdit *m_search = nullptr;
    QProgressBar *m_progress = nullptr;

    // Actions
    QAction *m_extractAct = nullptr;
    QAction *m_extractHereAct = nullptr;
    QAction *m_closeAct = nullptr;
    QAction *m_clearRecentAct = nullptr;

    // Services
    ArchiveService *m_svc = nullptr;
    ArchiveService *m_lst = nullptr;
    ArchiveService *m_preview = nullptr;

    // State
    QString m_path;
    QVector<ArchiveEntry> m_entries;
    QStringList m_recents;
    int m_opCount = 0;
    QTimer *m_searchTimer = nullptr;
};
