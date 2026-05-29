#include "ArchiveService.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QRegularExpression>


ArchiveService::ArchiveService(QObject *parent) : QObject(parent) {}

// ═══════════════════════════════════════════════════════════════════════════
// Tool helpers
// ═══════════════════════════════════════════════════════════════════════════
QString ArchiveService::bundledTool()
{
    QString bundle = QCoreApplication::applicationDirPath() + "/../Resources/bin/7za";
    if (QFileInfo::exists(bundle)) return QFileInfo(bundle).absoluteFilePath();
    QString sidecar = QCoreApplication::applicationDirPath() + "/bin/7za";
    if (QFileInfo::exists(sidecar)) return QFileInfo(sidecar).absoluteFilePath();
    QStringList paths = {"/opt/homebrew/bin/7z","/usr/local/bin/7z","/usr/bin/7z",
                          "/opt/homebrew/bin/7za","/usr/local/bin/7za"};
    for (const auto &p : paths) if (QFileInfo::exists(p)) return p;
    return QStandardPaths::findExecutable("7z");
}

bool ArchiveService::isBundledAvailable() { return !bundledTool().isEmpty(); }

QString ArchiveService::formatSize(qint64 bytes)
{
    if (bytes < 1024) return QString::number(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString::number(kb, 'f', 1) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024) return QString::number(mb, 'f', 1) + " MB";
    return QString::number(mb / 1024.0, 'f', 2) + " GB";
}

bool ArchiveService::isArchive(const QString &path)
{
    static const QStringList exts = {"7z","zip","rar","tar","gz","bz2","xz","tgz","tbz2","txz"};
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "gz" && path.endsWith(".tar.gz", Qt::CaseInsensitive)) return true;
    if (ext == "bz2" && path.endsWith(".tar.bz2", Qt::CaseInsensitive)) return true;
    if (ext == "xz" && path.endsWith(".tar.xz", Qt::CaseInsensitive)) return true;
    return exts.contains(ext);
}

// ═══════════════════════════════════════════════════════════════════════════
// Process
// ═══════════════════════════════════════════════════════════════════════════
void ArchiveService::startProc(const QString &prog, const QStringList &args)
{
    // Clean up any previous process
    if (m_proc) {
        m_proc->disconnect();
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(2000);
        }
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    if (prog.isEmpty()) {
        emit errorOccurred("No archive tool (7za/7z) found on system.");
        return;
    }

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::errorOccurred, this, [this, prog](QProcess::ProcessError e) {
        // finished() will NOT be emitted on FailedToStart — emit error here
        QString msg;
        switch (e) {
        case QProcess::FailedToStart: msg = "Tool failed to start (not found or no permission)"; break;
        case QProcess::Crashed:       msg = "Tool crashed"; break;
        case QProcess::Timedout:      msg = "Tool timed out"; break;
        case QProcess::WriteError:    msg = "Write error"; break;
        case QProcess::ReadError:     msg = "Read error"; break;
        default:                      msg = "Unknown process error"; break;
        }
        emit errorOccurred(msg + "\n" + prog);
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ArchiveService::onProcessFinished);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        m_out += m_proc->readAllStandardOutput();
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        QByteArray err = m_proc->readAllStandardError();
        m_out += err;
        if (m_op == Extract || m_op == Create || m_op == Preview) {
            QString s = QString::fromUtf8(err);
            static QRegularExpression re(R"((\d+)%)");
            auto m = re.match(s);
            if (m.hasMatch()) emit progressChanged(m.captured(1).toInt(), "Working...");
        }
    });
    m_out.clear();
    m_timer.start();
    m_proc->start(prog, args);
}

void ArchiveService::cancel()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) m_proc->kill();
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════
void ArchiveService::listContents(const QString &archivePath)
{
    m_op = List;
    QString tool = bundledTool();
    if (tool.isEmpty()) {
        emit errorOccurred("7za not found. Install p7zip or ensure the bundled binary is present.");
        return;
    }
    startProc(tool, {"l", "-ba", archivePath});
}

void ArchiveService::extractArchive(const QString &archivePath, const QString &destination,
                                    const QString &password)
{
    m_op = Extract;
    QDir().mkpath(destination);
    QString tool = bundledTool();
    if (tool.isEmpty()) {
        emit errorOccurred("No archive tool available.");
        return;
    }
    QStringList a{"x", archivePath, "-o" + destination, "-y"};
    if (!password.isEmpty()) a << "-p" + password;
    startProc(tool, a);
}

void ArchiveService::createArchive(const QStringList &files, const QString &destination,
                                   const QString &format, const QString &password,
                                   int compressionLevel)
{
    m_op = Create;
    QString tool = bundledTool();
    if (tool.isEmpty()) {
        emit errorOccurred("No archive tool available.");
        return;
    }
    QStringList a{"a", "-t" + format, destination,
                  "-mx=" + QString::number(compressionLevel), "-y"};
    if (!password.isEmpty())
        a << "-p" + password << (format == "7z" ? "-mhe=on" : "");
    // Files: if a single directory, use its path so 7za preserves structure
    for (const auto &f : files) a << f;
    startProc(tool, a);
}

void ArchiveService::startPreview(const QString &archivePath, const QString &filePath)
{
    QString tool = bundledTool();
    if (tool.isEmpty()) { emit errorOccurred("No archive tool available"); return; }

    m_previewDir = std::make_unique<QTemporaryDir>();
    if (!m_previewDir->isValid()) { emit errorOccurred("Cannot create temp dir"); return; }

    m_previewEntryPath = filePath;
    m_op = Preview;
    startProc(tool, {"x", archivePath, "-o" + m_previewDir->path(), "-y", "-aoa", filePath});
}

// ═══════════════════════════════════════════════════════════════════════════
// Process callback
// ═══════════════════════════════════════════════════════════════════════════
void ArchiveService::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    // Read any remaining buffered data
    if (m_proc) {
        m_out += m_proc->readAllStandardOutput();
        m_out += m_proc->readAllStandardError();
    }

    if (status != QProcess::NormalExit || exitCode != 0) {
        QString err;
        if (m_proc) err = QString::fromUtf8(m_proc->readAllStandardError());
        if (err.isEmpty()) err = QString::fromUtf8(m_out).left(500);
        emit errorOccurred(err.isEmpty()
            ? QString("Process exit code %1").arg(exitCode)
            : err.trimmed());
        if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
        return;
    }

    emit progressChanged(100, "Done");
    switch (m_op) {
    case List:    handleList(); break;
    case Extract: handleExtract(); break;
    case Create:  handleCreate(); break;
    case Preview: handlePreview(); break;
    default: break;
    }
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
}

// ═══════════════════════════════════════════════════════════════════════════
// Parsers
// ═══════════════════════════════════════════════════════════════════════════
QVector<ArchiveEntry> ArchiveService::parse7zList(const QString &data)
{
    QVector<ArchiveEntry> entries;
    for (const auto &line : data.split('\n', Qt::SkipEmptyParts)) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        // Normalize tabs → spaces
        QString s = trimmed;
        s.replace('\t', ' ');
        // Split on 2+ spaces: ["date time attr", size, compressed?, name]
        QStringList parts = s.split(QRegularExpression(R"(\s{2,})"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        // First part = "2026-05-28 22:03:46 ....A" → split by 1+ space
        QStringList head = parts[0].split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);

        ArchiveEntry e;
        if (head.size() >= 3) {
            e.date = QDateTime::fromString(head[0] + " " + head[1], "yyyy-MM-dd HH:mm:ss");
            if (!e.date.isValid())
                e.date = QDateTime::fromString(head[0] + " " + head[1], "yyyy-MM-dd HH:mm");
            e.isFolder = head[2].startsWith('D');
        }
        if (!e.date.isValid()) e.date = QDateTime::currentDateTime();

        // Remaining parts: [size, (compressed?), name]
        if (parts.size() == 2) {
            // Only size and name
            e.size = parts[1].toLongLong();
        } else if (parts.size() >= 3) {
            e.size = parts[1].toLongLong();
            // If parts.size() == 3: [size, compressed, name]
            // If parts.size() == 4: [size, compressed, name] with extra — use middle
            int compressedIdx = (parts.size() == 3) ? 2 : (parts.size() - 1);
            // Try compressed at parts[2], fall back to trying other positions
            QString compressedVal = parts[2].trimmed();
            bool ok = false;
            e.compressedSize = compressedVal.toLongLong(&ok);
            if (!ok) compressedVal.clear();
            // Name = last part
            e.name = parts.last().trimmed();
        }
        if (e.name.isEmpty()) e.name = parts.last().trimmed();
        e.path = e.name;
        if (!e.name.isEmpty())
            entries.append(e);
    }
    return entries;
}

QVector<ArchiveEntry> ArchiveService::parseZipList(const QString &data)
{
    QVector<ArchiveEntry> entries;
    bool ds = false;
    for (const auto &line : data.split('\n')) {
        if (line.contains("---")) { ds = true; continue; }
        if (!ds || line.startsWith("------")) continue;
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        if (p.size() < 4) continue;
        ArchiveEntry e;
        e.size = p[0].toLongLong();
        e.date = QDateTime::fromString(p[1] + " " + p[2], "yyyy-MM-dd HH:mm");
        if (!e.date.isValid()) e.date = QDateTime::currentDateTime();
        e.name = p.mid(3).join(' ');
        e.isFolder = e.name.endsWith('/');
        e.path = e.name;
        entries.append(e);
    }
    return entries;
}

QVector<ArchiveEntry> ArchiveService::parseTarList(const QString &data)
{
    QVector<ArchiveEntry> entries;
    for (const auto &line : data.split('\n', Qt::SkipEmptyParts)) {
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        if (p.size() < 5) continue;
        ArchiveEntry e;
        e.isFolder = p[0].startsWith('d') || p.last().endsWith('/');
        e.size = p[2].toLongLong();
        e.date = QDateTime::fromString(p[3] + " " + p[4], "MMM dd HH:mm yyyy");
        if (!e.date.isValid())
            e.date = QDateTime::fromString(p[3] + " " + p[4], "MMM  d HH:mm yyyy");
        if (!e.date.isValid()) e.date = QDateTime::currentDateTime();
        e.name = p.mid(5).join(' ');
        e.path = e.name;
        entries.append(e);
    }
    return entries;
}

void ArchiveService::handleList()
{
    emit contentsReady(parse7zList(QString::fromUtf8(m_out)));
}

void ArchiveService::handleExtract()
{
    emit finished(true, "Extraction complete. (" + QString::number(m_timer.elapsed()/1000) + "s)");
}

void ArchiveService::handleCreate()
{
    emit finished(true, "Archive created. (" + QString::number(m_timer.elapsed()/1000) + "s)");
}

void ArchiveService::handlePreview()
{
    if (!m_previewDir || !m_proc) { emit errorOccurred("Preview failed"); return; }
    QString entryPath = m_previewEntryPath;

    QString fullPath = m_previewDir->path() + "/" + entryPath;
    if (!QFileInfo::exists(fullPath)) {
        emit errorOccurred("Extracted file not found: " + entryPath);
        return;
    }
    emit previewReady(fullPath);
}
