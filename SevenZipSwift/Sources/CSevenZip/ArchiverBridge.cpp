#include "include/ArchiverC.h"

#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <regex>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <mutex>
#include <unordered_map>

#if defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include <mach-o/dyld.h>
#endif

// ══════════════════════════════════════════════════════════════════════
// C++ Implementation (hidden from C API users)
// ══════════════════════════════════════════════════════════════════════

enum class CAction { None, List, Extract, Create };

struct InternalEntry {
    std::string name;
    std::string path;
    int64_t size = 0;
    int64_t compressedSize = 0;
    bool isFolder = false;
    std::string date;
};

struct ArchiverCore {
    CAction action = CAction::None;
    int processId = -1;
    bool cancelled = false;

    std::vector<InternalEntry> listArchive(const std::string &archivePath, const std::string &password, std::string &error);
    bool extractArchive(const std::string &archivePath, const std::string &destination,
                        const std::string &password, std::string &error,
                        CProgressCallback progress, void *context);
    bool createArchive(const std::vector<std::string> &files, const std::string &destination,
                       const std::string &format, int compressionLevel,
                       const std::string &password, std::string &error,
                       CProgressCallback progress, void *context);
    void cancel();

    static bool isArchive(const std::string &path);
    static std::string findTool();
    static std::vector<std::string> findTools();
    static bool isToolAvailable();

private:
    bool executeTool(const std::string &prog, const std::vector<std::string> &args,
                     std::string &output, std::string &error,
                     CProgressCallback progressCallback, void *context,
                     const std::string *inputData = nullptr);
    bool createCompoundArchive(const std::vector<std::string> &files,
                               const std::string &destination,
                               const std::string &innerFormat,
                               const std::string &compressionFormat,
                               int compressionLevel,
                               const std::string &password,
                               std::string &error,
                               CProgressCallback progress, void *context);
    std::vector<InternalEntry> parseOutput(const std::string &data);
};

// ── Lifecycle ──────────────────────────────────────────────────────────

void *archiver_create(void) {
    return new ArchiverCore();
}

void archiver_destroy(void *handle) {
    delete static_cast<ArchiverCore *>(handle);
}

void archiver_cancel(void *handle) {
    auto *arch = static_cast<ArchiverCore *>(handle);
    arch->cancel();
}

// ── List ───────────────────────────────────────────────────────────────

CArchiveEntryList *archiver_list(void *handle, const char *path, const char *password, char **error) {
    auto *arch = static_cast<ArchiverCore *>(handle);
    std::string pwd = password ?: "";
    std::string err;
    auto entries = arch->listArchive(path ?: "", pwd, err);
    if (!err.empty()) {
        if (error) *error = strdup(err.c_str());
        auto *list = new CArchiveEntryList{nullptr, 0};
        return list;
    }

    auto *list = new CArchiveEntryList;
    list->count = static_cast<int>(entries.size());
    list->entries = new CArchiveEntry[list->count];
    for (int i = 0; i < list->count; ++i) {
        list->entries[i].name = strdup(entries[i].name.c_str());
        list->entries[i].path = strdup(entries[i].path.c_str());
        list->entries[i].size = entries[i].size;
        list->entries[i].compressedSize = entries[i].compressedSize;
        list->entries[i].isFolder = entries[i].isFolder;
        list->entries[i].date = strdup(entries[i].date.c_str());
    }
    return list;
}

// ── Extract ────────────────────────────────────────────────────────────

bool archiver_extract(void *handle, const char *path, const char *dest,
                      const char *password, char **error,
                      CProgressCallback progress, void *context) {
    auto *arch = static_cast<ArchiverCore *>(handle);
    std::string pwd = password ?: "";
    std::string err;
    bool ok = arch->extractArchive(path ?: "", dest ?: "", pwd, err, progress, context);
    if (!ok && error) *error = strdup(err.c_str());
    return ok;
}

// ── Create ─────────────────────────────────────────────────────────────

bool archiver_create_archive(void *handle, const char *const *files, int fileCount,
                             const char *dest, const char *format, int level,
                             const char *password, char **error,
                             CProgressCallback progress, void *context) {
    auto *arch = static_cast<ArchiverCore *>(handle);
    std::vector<std::string> cFiles;
    for (int i = 0; i < fileCount; ++i) {
        if (files[i]) cFiles.emplace_back(files[i]);
    }
    std::string pwd = password ?: "";
    std::string err;
    bool ok = arch->createArchive(cFiles, dest ?: "", format ?: "", level, pwd, err, progress, context);
    if (!ok && error) *error = strdup(err.c_str());
    return ok;
}

// ── Memory ─────────────────────────────────────────────────────────────

void archiver_free_entries(CArchiveEntryList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; ++i) {
        free(const_cast<char *>(list->entries[i].name));
        free(const_cast<char *>(list->entries[i].path));
        free(const_cast<char *>(list->entries[i].date));
    }
    delete[] list->entries;
    delete list;
}

void archiver_free_string(char *str) {
    free(str);
}

// ── Statics ────────────────────────────────────────────────────────────

bool archiver_is_archive(const char *path) {
    return ArchiverCore::isArchive(path ?: "");
}

bool archiver_is_tool_available(void) {
    return ArchiverCore::isToolAvailable();
}

char *archiver_find_tool(void) {
    std::string tool = ArchiverCore::findTool();
    if (tool.empty()) return nullptr;
    return strdup(tool.c_str());
}

// ══════════════════════════════════════════════════════════════════════
// ArchiverCore Implementation
// ══════════════════════════════════════════════════════════════════════

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char c) { return !std::isspace(c); }).base(),
            s.end());
}

/// Parse a (possibly blank or space-padded) numeric column. Never throws.
int64_t parseInt64(const std::string &field) {
    int64_t value = 0;
    bool any = false;
    for (unsigned char c : field) {
        if (std::isdigit(c)) {
            value = value * 10 + (c - '0');
            any = true;
        } else if (any) {
            break;
        }
    }
    return any ? value : 0;
}

/// Longest-suffix match of a destination path to a 7za format name.
/// Returns "" when the extension is not recognised.
std::string inferFormat(const std::string &destination) {
    static const std::vector<std::pair<std::string, std::string>> suffixes = {
        {".tar.gz", "tar.gz"},   {".tgz", "tar.gz"},
        {".tar.bz2", "tar.bz2"}, {".tbz2", "tar.bz2"}, {".tbz", "tar.bz2"},
        {".tar.xz", "tar.xz"},   {".txz", "tar.xz"},
        {".7z", "7z"},           {".zip", "zip"},      {".tar", "tar"},
        {".gz", "gzip"},         {".bz2", "bzip2"},    {".xz", "xz"},
    };
    std::string lower = toLower(destination);
    for (const auto &[suffix, format] : suffixes) {
        if (lower.size() > suffix.size() &&
            lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return format;
        }
    }
    return "";
}

/// Name the intermediate tar after the final archive, so that decompressing
/// e.g. `photos.tar.gz` yields `photos.tar` rather than a temp-file name.
std::string innerTarName(const std::string &destination) {
    auto slash = destination.rfind('/');
    std::string base = (slash == std::string::npos) ? destination
                                                     : destination.substr(slash + 1);
    std::string lower = toLower(base);
    static const std::vector<std::string> stripped = {
        ".gz", ".bz2", ".xz", ".tgz", ".tbz2", ".tbz", ".txz"
    };
    for (const auto &suffix : stripped) {
        if (lower.size() > suffix.size() &&
            lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
            // ".tgz" and friends collapse the ".tar" too — put it back below.
            base = base.substr(0, base.size() - suffix.size());
            lower = toLower(base);
            break;
        }
    }
    if (base.empty()) base = "archive";
    if (!(lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".tar") == 0)) {
        base += ".tar";
    }
    return base;
}

/// Ask a candidate binary to describe itself. A genuine 7-Zip/p7zip engine
/// answers `i` with its format table and exits 0.
bool probeEngine(const std::string &path) {
#if defined(__APPLE__)
    int fds[2];
    if (pipe(fds) < 0) return false;

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return false; }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
        execl(path.c_str(), path.c_str(), "i", static_cast<char *>(nullptr));
        _exit(127);
    }

    close(fds[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) out.append(buf, n);
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
           out.find("7-Zip") != std::string::npos;
#else
    (void)path;
    return false;
#endif
}

/// Cached form of probeEngine. Guards against picking up a binary that merely
/// happens to be called `7z` — this project installs its own CLI as
/// /usr/local/bin/7z, and driving that as an engine would be nonsense.
bool isUsableEngine(const std::string &path) {
    static std::mutex mutex;
    static std::unordered_map<std::string, bool> cache;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    bool ok = probeEngine(path);
    cache.emplace(path, ok);
    return ok;
}

/// True when the engine could read the archive but not decode its contents,
/// which means the engine is too old rather than the file being unreadable.
bool looksUndecodable(const std::string &err) {
    return toLower(err).find("unsupported method") != std::string::npos;
}

/// True when an engine reports it simply cannot read the file as an archive —
/// the signature of a missing codec, which a fuller engine may still handle.
/// Password failures are deliberately excluded: they are a real answer, and
/// callers key the password prompt off that message.
bool looksUnsupported(const std::string &err) {
    std::string e = toLower(err);
    if (e.find("password") != std::string::npos ||
        e.find("encrypted") != std::string::npos) {
        return false;
    }
    return e.find("can not open the file as archive") != std::string::npos ||
           e.find("cannot open the file as archive") != std::string::npos ||
           e.find("is not supported archive") != std::string::npos ||
           e.find("unsupported archive") != std::string::npos ||
           e.find("unsupported method") != std::string::npos;
}

std::string baseName(const std::string &path) {
    auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/// Message shown when no installed engine can read the format at all.
std::string unsupportedFormatError(const std::string &path) {
    return "Cannot open " + baseName(path) + ": no available archive engine "
           "supports this format.\nThe bundled engine handles 7z, zip, tar, gzip, "
           "bzip2, xz and zstd. RAR, ISO, DMG, WIM and similar formats need a "
           "fuller engine:\n  brew install sevenzip";
}

/// Message shown when an engine reads the archive but cannot decode it — the
/// case for RAR5 archives written with a method p7zip 17.x does not implement.
std::string undecodableError(const std::string &path) {
    return baseName(path) + " uses a compression method that no available "
           "archive engine can decode, so no data could be recovered.\n"
           "This is common for newer RAR archives. Installing the official "
           "7-Zip usually fixes it:\n  brew install sevenzip";
}

std::string tempDirRoot() {
    const char *tmp = getenv("TMPDIR");
    std::string root = (tmp && *tmp) ? tmp : "/tmp";
    if (!root.empty() && root.back() == '/') root.pop_back();
    return root;
}

}  // namespace

std::vector<InternalEntry> ArchiverCore::listArchive(const std::string &archivePath, const std::string &password, std::string &error) {
    action = CAction::List;
    cancelled = false;
    std::vector<std::string> tools = findTools();
    if (tools.empty()) { error = "Archive engine not found."; return {}; }

    std::vector<std::string> args = {"l", "-ba", archivePath};
    if (!password.empty()) args.push_back("-p" + password);

    // The bundled engine is a reduced build; if it cannot read this format,
    // fall through to a fuller one rather than reporting an empty archive.
    std::string lastErr;
    bool allUnsupported = true;
    bool sawUndecodable = false;
    for (const auto &tool : tools) {
        std::string output;
        std::string err;
        if (executeTool(tool, args, output, err, nullptr, nullptr)) {
            return parseOutput(output);
        }
        lastErr = err;
        if (looksUndecodable(err)) sawUndecodable = true;
        if (!looksUnsupported(err)) { allUnsupported = false; break; }
        if (cancelled) { allUnsupported = false; break; }
    }

    if (sawUndecodable) {
        error = undecodableError(archivePath);
    } else if (allUnsupported) {
        error = unsupportedFormatError(archivePath);
    } else {
        error = lastErr.empty() ? "Failed to list archive" : lastErr;
    }
    return {};
}

bool ArchiverCore::extractArchive(const std::string &archivePath, const std::string &destination,
                                   const std::string &password, std::string &error,
                                   CProgressCallback progressCallback, void *context) {
    action = CAction::Extract;
    cancelled = false;
    std::vector<std::string> tools = findTools();
    if (tools.empty()) { error = "No archive tool found."; return false; }

    std::vector<std::string> args = {"x", archivePath, "-o" + destination, "-y"};
    if (!password.empty()) { args.push_back("-p" + password); }

    std::string lastErr;
    bool allUnsupported = true;
    bool sawUndecodable = false;
    for (const auto &tool : tools) {
        std::string output;
        std::string err;
        if (executeTool(tool, args, output, err, progressCallback, context)) {
            return true;
        }
        lastErr = err;
        if (looksUndecodable(err)) sawUndecodable = true;
        if (!looksUnsupported(err)) { allUnsupported = false; break; }
        if (cancelled) { allUnsupported = false; break; }
    }

    // A failed decode still leaves the empty placeholder files the engine
    // created, so say so rather than letting them look like a result.
    if (sawUndecodable) {
        error = undecodableError(archivePath) +
                "\nAny files already written to the destination are incomplete.";
    } else if (allUnsupported) {
        error = unsupportedFormatError(archivePath);
    } else {
        error = lastErr.empty() ? "Extraction failed" : lastErr;
    }
    return false;
}

bool ArchiverCore::createArchive(const std::vector<std::string> &files, const std::string &destination,
                                  const std::string &format, int compressionLevel,
                                  const std::string &password, std::string &error,
                                  CProgressCallback progressCallback, void *context) {
    action = CAction::Create;
    cancelled = false;
    std::string tool = findTool();
    if (tool.empty()) { error = "No archive tool found."; return false; }

    // An empty format means "derive it from the destination extension".
    std::string fmt = toLower(format);
    if (fmt.empty()) {
        fmt = inferFormat(destination);
        if (fmt.empty()) fmt = "zip";
    }

    // Compound formats (tar.gz, tar.bz2, tar.xz) need two-step creation
    static const std::vector<std::string> compoundFormats = {"tar.gz", "tar.bz2", "tar.xz"};
    if (std::find(compoundFormats.begin(), compoundFormats.end(), fmt) != compoundFormats.end()) {
        std::string ext = fmt.substr(fmt.find('.') + 1);
        // Map extension to 7za format name: gz→gzip, bz2→bzip2, xz→xz
        static const std::unordered_map<std::string, std::string> fmtMap = {
            {"gz", "gzip"}, {"bz2", "bzip2"}, {"xz", "xz"}
        };
        auto it = fmtMap.find(ext);
        if (it == fmtMap.end()) { error = "Unknown compression format: " + ext; return false; }
        return createCompoundArchive(files, destination, "tar", it->second,
                                      compressionLevel, password, error,
                                      progressCallback, context);
    }

    std::vector<std::string> args = {"a", "-t" + fmt, destination,
                                     "-mx=" + std::to_string(compressionLevel), "-y"};
    if (!password.empty()) {
        args.push_back("-p" + password);
        if (fmt == "7z") args.push_back("-mhe=on");
    }
    for (const auto &f : files) args.push_back(f);
    std::string output;
    return executeTool(tool, args, output, error, progressCallback, context);
}

void ArchiverCore::cancel() {
    cancelled = true;
#if defined(__APPLE__)
    if (processId > 0) {
        kill(processId, SIGKILL);
        int status;
        waitpid(processId, &status, WNOHANG);
        processId = -1;
    }
#endif
}

bool ArchiverCore::isArchive(const std::string &path) {
    // Kept in sync with CFBundleTypeExtensions in scripts/make-app-bundle.sh —
    // Finder offers "Open With 7-Zip" for each of these, so each must open.
    static const char *exts[] = {
        "7z","zip","rar","tar","gz","bz2","xz","tgz","tbz2","tbz","txz","taz",
        "z","lzma","lz4","zst","iso","cab","arj","lzh","lha","wim","swm","esd",
        "dmg","hfs","vhd","vhdx","vmdk","cpio","rpm","deb","chm","msi","udf",
        "squashfs","apfs","qcow2","fat","ntfs","mbr","gpt","xar","pkg"
    };
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == "gz" && path.size() > 7) {
        std::string base = path.substr(path.size() - 7);
        std::transform(base.begin(), base.end(), base.begin(), ::tolower);
        if (base == ".tar.gz") return true;
    }
    if (ext == "bz2" && path.size() > 8) {
        std::string base = path.substr(path.size() - 8);
        std::transform(base.begin(), base.end(), base.begin(), ::tolower);
        if (base == ".tar.bz2") return true;
    }
    if (ext == "xz" && path.size() > 7) {
        std::string base = path.substr(path.size() - 7);
        std::transform(base.begin(), base.end(), base.begin(), ::tolower);
        if (base == ".tar.xz") return true;
    }
    for (const char *e : exts) {
        if (ext == e) return true;
    }
    return false;
}

std::vector<std::string> ArchiverCore::findTools() {
    // Engines in preference order. The bundled 7za comes first so the app
    // stays self-contained, but it is the reduced p7zip build: it has no RAR,
    // ISO, DMG, WIM (etc.) codecs. A fuller system 7z/7zz is therefore kept as
    // a fallback, and callers retry down this list when an engine reports that
    // it cannot open a file as an archive.
    std::vector<std::string> tools;
#if defined(__APPLE__)
    auto exists = [](const std::string &p) -> bool { return access(p.c_str(), X_OK) == 0; };
    auto add = [&tools](const std::string &p) {
        if (p.empty()) return;
        if (std::find(tools.begin(), tools.end(), p) != tools.end()) return;
        if (!isUsableEngine(p)) return;
        tools.push_back(p);
    };

    // 1. The copy embedded in the app bundle, then an executable-relative one.
    char exeBuf[PATH_MAX];
    uint32_t exeSize = sizeof(exeBuf);
    if (_NSGetExecutablePath(exeBuf, &exeSize) == 0) {
        std::string exePath(exeBuf);
        auto slash = exePath.rfind('/');
        if (slash != std::string::npos) {
            std::string exeDir = exePath.substr(0, slash);
            // In a proper .app bundle: App.app/Contents/MacOS/ -> ../Resources/bin/7za
            std::string bundled = exeDir + "/../Resources/bin/7za";
            char *resolved = realpath(bundled.c_str(), nullptr);
            if (resolved) {
                std::string resolvedStr(resolved);
                ::free(resolved);
                if (exists(resolvedStr)) add(resolvedStr);
            }
            // Sidecar: executable-relative bin/7za
            std::string sidecar = exeDir + "/bin/7za";
            if (exists(sidecar)) add(sidecar);
        }
    }

    // 2. Well-known system paths. 7z/7zz ship the full codec set, so they are
    //    tried before a system 7za.
    static const char *candidates[] = {
        "/opt/homebrew/bin/7z", "/usr/local/bin/7z", "/usr/bin/7z",
        "/opt/homebrew/bin/7zz", "/usr/local/bin/7zz", "/usr/bin/7zz",
        "/opt/homebrew/bin/7za", "/usr/local/bin/7za"
    };
    for (const char *p : candidates) {
        if (exists(p)) add(p);
    }

    // 3. Anything else on PATH.
    const char *pathEnv = getenv("PATH");
    if (pathEnv) {
        std::istringstream ss(std::string{pathEnv});
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (dir.empty()) continue;
            for (const char *name : {"/7z", "/7zz", "/7za"}) {
                std::string c = dir + name;
                if (exists(c)) add(c);
            }
        }
    }
#endif
    return tools;
}

std::string ArchiverCore::findTool() {
    auto tools = findTools();
    return tools.empty() ? std::string{} : tools.front();
}

bool ArchiverCore::isToolAvailable() {
    return !findTool().empty();
}

bool ArchiverCore::createCompoundArchive(const std::vector<std::string> &files,
                                          const std::string &destination,
                                          const std::string &innerFormat,
                                          const std::string &compressionFormat,
                                          int compressionLevel,
                                          const std::string &password,
                                          std::string &error,
                                          CProgressCallback progress, void *context) {
    // Step 1: Create the inner tar inside a private temp directory.
    //
    // 7za appends the format extension when the output path has none, so the
    // tar must already end in ".tar" or it lands somewhere we do not expect.
    // It is named after the final archive so that decompressing `x.tar.gz`
    // yields `x.tar` rather than a temp-file name.
    std::string tool = findTool();
    std::string dirTemplate = tempDirRoot() + "/7z-XXXXXX";
    std::vector<char> dirBuf(dirTemplate.begin(), dirTemplate.end());
    dirBuf.push_back('\0');
    if (!mkdtemp(dirBuf.data())) { error = "Failed to create temp directory"; return false; }
    std::string tempDir(dirBuf.data());
    std::string tarPath = tempDir + "/" + innerTarName(destination);

    auto cleanup = [&]() {
        unlink(tarPath.c_str());
        rmdir(tempDir.c_str());
    };

    std::vector<std::string> innerArgs = {"a", "-t" + innerFormat, tarPath, "-y"};
    for (const auto &f : files) innerArgs.push_back(f);

    std::string innerOutput;
    std::string innerErr;
    bool innerOk = executeTool(tool, innerArgs, innerOutput, innerErr, nullptr, nullptr);
    if (!innerOk) {
        error = innerErr.empty() ? "Failed to create intermediate archive" : innerErr;
        cleanup();
        return false;
    }

    if (cancelled) { cleanup(); error = "Cancelled"; return false; }

    // Step 2: Compress the tar into the final archive. gzip/bzip2/xz are
    // single-stream formats that 7za cannot append to, so an existing
    // destination has to be replaced. Build the result beside it under a
    // temporary name and rename it into place only once it is complete —
    // a failure here must not destroy the archive that was already there.
    std::string stagedPath = destination + ".7z-part" + std::to_string(getpid());
    unlink(stagedPath.c_str());

    std::vector<std::string> compressArgs = {"a", "-t" + compressionFormat, stagedPath, tarPath,
                                              "-mx=" + std::to_string(compressionLevel), "-y"};
    if (!password.empty()) {
        compressArgs.push_back("-p" + password);
    }

    std::string compressOutput;
    bool ok = executeTool(tool, compressArgs, compressOutput, error, progress, context);
    cleanup();

    if (!ok) {
        unlink(stagedPath.c_str());
        return false;
    }
    if (rename(stagedPath.c_str(), destination.c_str()) != 0) {
        error = "Failed to write " + destination + ": " + strerror(errno);
        unlink(stagedPath.c_str());
        return false;
    }
    return true;
}

bool ArchiverCore::executeTool(const std::string &prog, const std::vector<std::string> &args,
                                std::string &output, std::string &error,
                                CProgressCallback progressCallback, void *context,
                                const std::string *inputData) {
#if defined(__APPLE__)
    std::vector<const char *> argv;
    argv.push_back(prog.c_str());
    for (const auto &a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        error = "Failed to create pipes";
        return false;
    }
    if (inputData && pipe(stdin_pipe) < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        error = "Failed to create stdin pipe";
        return false;
    }

    if (getenv("SEVENZIP_DEBUG")) {
        std::string line = "[7z] exec: " + prog;
        for (const auto &a : args) line += " " + a;
        line += "\n";
        write(STDERR_FILENO, line.c_str(), line.size());
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        error = "Failed to fork process";
        return false;
    }

    if (pid == 0) {
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        if (stdin_pipe[1] >= 0) {
            close(stdin_pipe[1]); dup2(stdin_pipe[0], STDIN_FILENO); close(stdin_pipe[0]);
        } else {
            // Never let the child inherit our stdin: 7za prompts for a
            // password on an encrypted archive and would block forever
            // waiting for input nobody is there to type.
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
        }
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]); close(stderr_pipe[1]);
        execvp(prog.c_str(), const_cast<char *const *>(argv.data()));
        _exit(127);
    }

    processId = pid;
    close(stdout_pipe[1]); close(stderr_pipe[1]);
    if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);

    // Write input data to child's stdin if provided
    if (inputData && stdin_pipe[1] >= 0) {
        const char *data = inputData->data();
        size_t remaining = inputData->size();
        while (remaining > 0) {
            ssize_t written = write(stdin_pipe[1], data, remaining);
            if (written < 0) break;
            data += written;
            remaining -= written;
        }
        close(stdin_pipe[1]);
    }

    std::string stdOut, stdErr;
    char buf[4096];
    ssize_t n;

    int flagsOut = fcntl(stdout_pipe[0], F_GETFL, 0);
    int flagsErr = fcntl(stderr_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flagsOut | O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, flagsErr | O_NONBLOCK);

    while (true) {
        if (cancelled) { kill(pid, SIGKILL); break; }
        bool gotData = false;
        while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
            stdOut.append(buf, n); gotData = true;
        }
        while ((n = read(stderr_pipe[0], buf, sizeof(buf))) > 0) {
            stdErr.append(buf, n); gotData = true;
        }

        if (progressCallback && !stdErr.empty()) {
            static std::regex progressRe(R"((\d+)%)");
            std::smatch m;
            if (std::regex_search(stdErr, m, progressRe)) {
                progressCallback(std::stoi(m[1].str()), context);
            }
        }

        if (!gotData) {
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                char last[4096];
                while ((n = read(stdout_pipe[0], last, sizeof(last))) > 0) stdOut.append(last, n);
                while ((n = read(stderr_pipe[0], last, sizeof(last))) > 0) stdErr.append(last, n);
                close(stdout_pipe[0]); close(stderr_pipe[0]);
                output = stdOut;
                processId = -1;
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    if (progressCallback) progressCallback(100, context);
                    return true;
                }
                error = stdErr.empty() ? ("Exit code " + std::to_string(WEXITSTATUS(status))) : stdErr;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    close(stdout_pipe[0]); close(stderr_pipe[0]);
    waitpid(pid, nullptr, 0);
    processId = -1;
    error = "Cancelled";
    return false;
#else
    error = "Unsupported platform";
    return false;
#endif
}

std::vector<InternalEntry> ArchiverCore::parseOutput(const std::string &data) {
    // `7za l -ba` emits fixed-width columns:
    //
    //   2026-09-02 12:16:42 D....            0            0  path/to/name
    //   |0-9      |11-18    |20-24 |26-37        |39-50      |53+
    //
    // The packed column is blank for entries inside a solid block, and the
    // name begins at column 53 — so it is read by offset rather than by
    // splitting on whitespace, which would corrupt names containing runs of
    // two or more spaces.
    constexpr size_t kNameColumn = 53;

    auto hasDatePrefix = [](const std::string &l) {
        if (l.size() < 19) return false;
        auto digit = [&](size_t i) { return std::isdigit(static_cast<unsigned char>(l[i])) != 0; };
        return digit(0) && digit(1) && digit(2) && digit(3) && l[4] == '-' &&
               digit(5) && digit(6) && l[7] == '-' && digit(8) && digit(9) && l[10] == ' ';
    };

    std::vector<InternalEntry> entries;
    std::istringstream stream(data);
    std::string line;

    while (std::getline(stream, line)) {
        rtrim(line);  // drop any trailing CR from the pipe
        if (line.empty()) continue;

        InternalEntry entry;

        if (hasDatePrefix(line) && line.size() > kNameColumn) {
            entry.date = line.substr(0, 19);
            entry.isFolder = line[20] == 'D';

            if (line[51] == ' ' && line[52] == ' ' && line[kNameColumn] != ' ') {
                // Columns are intact: read each field at its fixed offset.
                entry.size = parseInt64(line.substr(26, 12));
                entry.compressedSize = parseInt64(line.substr(39, 12));
                entry.name = line.substr(kNameColumn);
            } else {
                // A size wider than its column shifts everything right; fall
                // back to reading the attr/size/packed tokens in order.
                std::istringstream fields(line.substr(19));
                std::string attr, size, packed;
                fields >> attr >> size >> packed;
                entry.size = parseInt64(size);
                entry.compressedSize = parseInt64(packed);
                std::streamoff consumed = fields.tellg();
                if (consumed > 0) {
                    size_t nameStart = 19 + static_cast<size_t>(consumed);
                    while (nameStart < line.size() && line[nameStart] == ' ') ++nameStart;
                    if (nameStart < line.size()) entry.name = line.substr(nameStart);
                }
            }
        } else {
            // Not a listing row in the expected shape — take the trailing
            // token so unusual output still yields something usable.
            std::istringstream fields(line);
            std::string token;
            while (fields >> token) entry.name = token;
        }

        rtrim(entry.name);
        if (entry.name.empty()) continue;
        entry.path = entry.name;
        entries.push_back(entry);
    }
    return entries;
}
