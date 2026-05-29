#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <QProcess>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <memory>

struct ArchiveEntry {
    QString name;
    QString path;
    qint64 size = 0;
    qint64 compressedSize = 0;
    bool isFolder = false;
    QDateTime date;
};

class ArchiveService : public QObject
{
    Q_OBJECT
public:
    explicit ArchiveService(QObject *parent = nullptr);

    void listContents(const QString &archivePath);
    void extractArchive(const QString &archivePath, const QString &destination,
                        const QString &password = {});
    void createArchive(const QStringList &files, const QString &destination,
                       const QString &format, const QString &password = {},
                       int compressionLevel = 5);
    void startPreview(const QString &archivePath, const QString &filePath);
    void cancel();

    static QString bundledTool();
    static bool isBundledAvailable();
    static QString formatSize(qint64 bytes);
    static bool isArchive(const QString &path);

signals:
    void contentsReady(const QVector<ArchiveEntry> &entries);
    void finished(bool success, const QString &message);
    void errorOccurred(const QString &error);
    void progressChanged(int percent, const QString &status);
    void previewReady(const QString &tempPath);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    enum Op { None, List, Extract, Create, Preview };
    Op m_op = None;
    QProcess *m_proc = nullptr;
    QByteArray m_out;
    QElapsedTimer m_timer;
    std::unique_ptr<QTemporaryDir> m_previewDir;
    QString m_previewEntryPath;

    void startProc(const QString &prog, const QStringList &args);
    QVector<ArchiveEntry> parse7zList(const QString &data);
    QVector<ArchiveEntry> parseZipList(const QString &data);
    QVector<ArchiveEntry> parseTarList(const QString &data);
    void handleList();
    void handleExtract();
    void handleCreate();
    void handlePreview();
};
