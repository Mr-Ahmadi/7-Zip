#include "ArchiveService.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTimer>
#include <QHash>
#include <QFile>


ArchiveService::ArchiveService(QObject *parent) : QObject(parent) {}

// ═══════════════════════════════════════════════════════════════════════════
// Tool helpers
// ═══════════════════════════════════════════════════════════════════════════
/// A genuine 7-Zip/p7zip engine answers `i` with its format table and exits 0.
/// This project installs its own CLI as /usr/local/bin/7z, so a binary merely
/// named `7z` must be verified before it is driven as an engine.
static bool isUsableEngine(const QString &path)
{
    static QHash<QString, bool> cache;
    auto it = cache.constFind(path);
    if (it != cache.constEnd()) return it.value();

    QProcess probe;
    probe.start(path, {"i"});
    bool ok = probe.waitForFinished(4000) &&
              probe.exitStatus() == QProcess::NormalExit &&
              probe.exitCode() == 0 &&
              QString::fromUtf8(probe.readAllStandardOutput()).contains("7-Zip");
    cache.insert(path, ok);
    return ok;
}

QStringList ArchiveService::engineCandidates()
{
    // Preference order. The bundled 7za keeps the app self-contained but is the
    // reduced p7zip build with no RAR/ISO/DMG/WIM codecs, so fuller system
    // engines follow it as fallbacks.
    QStringList out;
    auto add = [&out](const QString &p) {
        if (p.isEmpty() || out.contains(p)) return;
        if (!QFileInfo::exists(p) || !isUsableEngine(p)) return;
        out << p;
    };

    const QString dir = QCoreApplication::applicationDirPath();
    add(QFileInfo(dir + "/../Resources/bin/7za").absoluteFilePath());
    add(QFileInfo(dir + "/bin/7za").absoluteFilePath());
    for (const QString &p : {QStringLiteral("/opt/homebrew/bin/7z"),
                             QStringLiteral("/usr/local/bin/7z"),
                             QStringLiteral("/usr/bin/7z"),
                             QStringLiteral("/opt/homebrew/bin/7zz"),
                             QStringLiteral("/usr/local/bin/7zz"),
                             QStringLiteral("/opt/homebrew/bin/7za"),
                             QStringLiteral("/usr/local/bin/7za")}) {
        add(p);
    }
    for (const QString &name : {QStringLiteral("7z"), QStringLiteral("7zz"), QStringLiteral("7za")}) {
        add(QStandardPaths::findExecutable(name));
    }
    return out;
}

QString ArchiveService::bundledTool()
{
    const QStringList tools = engineCandidates();
    return tools.isEmpty() ? QString() : tools.first();
}

bool ArchiveService::isBundledAvailable() { return !bundledTool().isEmpty(); }

/// The Unarchiver's `unar`, used as a last-resort extractor: no 7-Zip build
/// decodes RAR compression version 6 (WinRAR 7.0+), which lists fine but fails
/// on every entry.
static QString findUnar()
{
    for (const QString &p : {QStringLiteral("/opt/homebrew/bin/unar"),
                             QStringLiteral("/usr/local/bin/unar"),
                             QStringLiteral("/usr/bin/unar")}) {
        if (QFileInfo::exists(p)) return p;
    }
    return QStandardPaths::findExecutable("unar");
}

/// An engine reporting it cannot read the file may simply lack the codec, so a
/// fuller engine is worth trying. Password failures are excluded: they are a
/// real answer, and the UI keys its prompt off that message.
static bool looksUnsupported(const QString &err)
{
    const QString e = err.toLower();
    if (e.contains("password") || e.contains("encrypted")) return false;
    return e.contains("can not open the file as archive") ||
           e.contains("cannot open the file as archive") ||
           e.contains("is not supported archive") ||
           e.contains("unsupported archive") ||
           e.contains("unsupported method");
}

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
    static const QStringList exts = {
        "7z","zip","rar","tar","gz","bz2","xz","tgz","tbz2","tbz","txz","taz",
        "z","lzma","lz4","zst","iso","cab","arj","lzh","lha","wim","swm","esd",
        "dmg","hfs","vhd","vhdx","vmdk","cpio","rpm","deb","chm","msi","udf",
        "squashfs","apfs","qcow2","fat","ntfs","mbr","gpt","xar","pkg"
    };
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
    m_lastArgs = args;
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
    m_engineIndex = 0;
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
    m_engineIndex = 0;
    m_extractDest = destination;
    m_extractPassword = password;
    m_triedUnar = false;
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
    // tar.gz / tar.bz2 / tar.xz are not 7za format names: the tar has to be
    // built first and then compressed. Stage one runs here, stage two is
    // queued in m_pendingCompress and fired from handleCreate().
    static const QHash<QString, QString> compound = {
        {"tar.gz", "gzip"}, {"tar.bz2", "bzip2"}, {"tar.xz", "xz"}
    };
    m_compoundDir.reset();
    m_pendingCompress.clear();
    m_compoundStaged.clear();
    m_compoundFinal.clear();

    if (compound.contains(format)) {
        m_compoundDir = std::make_unique<QTemporaryDir>();
        if (!m_compoundDir->isValid()) {
            emit errorOccurred("Cannot create temporary directory");
            return;
        }
        // Name the tar after the final archive so that decompressing
        // `photos.tar.gz` yields `photos.tar`, not a temp-file name.
        QString base = QFileInfo(destination).fileName();
        for (const QString &suffix : {".gz", ".bz2", ".xz", ".tgz", ".tbz2", ".tbz", ".txz"}) {
            if (base.endsWith(suffix, Qt::CaseInsensitive)) {
                base.chop(suffix.size());
                break;
            }
        }
        if (base.isEmpty()) base = "archive";
        if (!base.endsWith(".tar", Qt::CaseInsensitive)) base += ".tar";
        const QString tarPath = m_compoundDir->filePath(base);

        // gzip/bzip2/xz cannot be appended to, so an existing destination has
        // to be replaced. Build the result beside it under a temporary name and
        // rename it into place only once it is complete, so a failure here
        // cannot destroy the archive that was already there.
        m_compoundStaged = destination + ".7z-part";
        m_compoundFinal = destination;
        QFile::remove(m_compoundStaged);

        m_pendingCompress = QStringList{"a", "-t" + compound.value(format), m_compoundStaged, tarPath,
                                        "-mx=" + QString::number(compressionLevel), "-y"};
        if (!password.isEmpty()) m_pendingCompress << "-p" + password;

        QStringList tarArgs{"a", "-ttar", tarPath, "-y"};
        for (const auto &f : files) tarArgs << f;
        startProc(tool, tarArgs);
        return;
    }

    QStringList a{"a", "-t" + format, destination,
                  "-mx=" + QString::number(compressionLevel), "-y"};
    if (!password.isEmpty()) {
        a << "-p" + password;
        if (format == "7z") a << "-mhe=on";
    }
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

        // The bundled engine is a reduced build; if it cannot read this format,
        // retry the same command on a fuller one rather than reporting failure.
        if ((m_op == List || m_op == Extract) && looksUnsupported(err)) {
            const QStringList tools = engineCandidates();
            if (m_engineIndex + 1 < tools.size()) {
                m_engineIndex++;
                const QString next = tools.at(m_engineIndex);
                const QStringList args = m_lastArgs;
                if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
                QTimer::singleShot(0, this, [this, next, args] { startProc(next, args); });
                return;
            }
            // Every 7-Zip engine is out; unar still handles newer RAR.
            const QString unar = m_op == Extract && !m_triedUnar ? findUnar() : QString();
            if (!unar.isEmpty()) {
                m_triedUnar = true;
                QStringList args{"-q", "-f", "-D", "-o", m_extractDest};
                if (!m_extractPassword.isEmpty()) args << "-p" << m_extractPassword;
                args << m_lastArgs.value(1);  // the archive path
                if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
                QTimer::singleShot(0, this, [this, unar, args] { startProc(unar, args); });
                return;
            }
        }

        m_pendingCompress.clear();
        m_compoundDir.reset();
        if (!m_compoundStaged.isEmpty()) { QFile::remove(m_compoundStaged); m_compoundStaged.clear(); }
        m_compoundFinal.clear();
        emit errorOccurred(err.isEmpty()
            ? QString("Process exit code %1").arg(exitCode)
            : err.trimmed());
        if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
        return;
    }

    if (m_pendingCompress.isEmpty()) emit progressChanged(100, "Done");
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
    // `7za l -ba` emits fixed-width columns:
    //
    //   2026-09-02 12:16:42 D....            0            0  path/to/name
    //   |0-9      |11-18    |20-24 |26-37        |39-50      |53+
    //
    // The packed column is blank for entries inside a solid block, and the
    // name begins at column 53 — so it is read by offset rather than by
    // splitting on whitespace, which would corrupt names containing runs of
    // two or more spaces.
    constexpr int kNameColumn = 53;
    static const QRegularExpression datePrefix(R"(^\d{4}-\d{2}-\d{2} )");
    static const QRegularExpression spaceRun(R"(\s+)");

    QVector<ArchiveEntry> entries;
    const QStringList lines = data.split('\n');
    for (QString line : lines) {
        while (line.endsWith('\r') || line.endsWith(' ')) line.chop(1);
        if (line.isEmpty()) continue;
        if (!datePrefix.match(line).hasMatch() || line.size() <= kNameColumn) continue;

        ArchiveEntry e;
        e.date = QDateTime::fromString(line.left(19), "yyyy-MM-dd HH:mm:ss");
        e.isFolder = line.at(20) == 'D';

        if (line.at(51) == ' ' && line.at(52) == ' ' && line.at(kNameColumn) != ' ') {
            e.size = line.mid(26, 12).trimmed().toLongLong();
            e.compressedSize = line.mid(39, 12).trimmed().toLongLong();
            e.name = line.mid(kNameColumn);
        } else {
            // A size wider than its column shifts everything right; fall back
            // to reading the attr/size/packed tokens in order.
            const QStringList fields = line.mid(19).split(spaceRun, Qt::SkipEmptyParts);
            if (fields.size() >= 2) e.size = fields.at(1).toLongLong();
            if (fields.size() >= 3) e.compressedSize = fields.at(2).toLongLong();
            e.name = fields.size() >= 4 ? fields.mid(3).join(' ') : QString();
        }

        e.name = e.name.trimmed();
        if (e.name.isEmpty()) continue;
        if (!e.date.isValid()) e.date = QDateTime::currentDateTime();
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
    if (!m_pendingCompress.isEmpty()) {
        const QStringList args = m_pendingCompress;
        m_pendingCompress.clear();
        QTimer::singleShot(0, this, [this, args] { startProc(bundledTool(), args); });
        return;
    }
    m_compoundDir.reset();

    if (!m_compoundStaged.isEmpty()) {
        const QString staged = m_compoundStaged;
        const QString finalPath = m_compoundFinal;
        m_compoundStaged.clear();
        m_compoundFinal.clear();
        QFile::remove(finalPath);
        if (!QFile::rename(staged, finalPath)) {
            QFile::remove(staged);
            emit errorOccurred("Failed to write " + finalPath);
            return;
        }
    }
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
