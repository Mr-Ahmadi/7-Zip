#include "include/ArchiverC.h"

#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <regex>
#include <cstring>

#if defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
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

    std::vector<InternalEntry> listArchive(const std::string &archivePath, std::string &error);
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
    static bool isToolAvailable();

private:
    bool executeTool(const std::string &prog, const std::vector<std::string> &args,
                     std::string &output, std::string &error,
                     CProgressCallback progressCallback, void *context);
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

CArchiveEntryList *archiver_list(void *handle, const char *path, char **error) {
    auto *arch = static_cast<ArchiverCore *>(handle);
    std::string err;
    auto entries = arch->listArchive(path ?: "", err);
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

std::vector<InternalEntry> ArchiverCore::listArchive(const std::string &archivePath, std::string &error) {
    action = CAction::List;
    cancelled = false;
    std::string output;
    std::string err;
    std::string tool = findTool();
    if (tool.empty()) { error = "7za not found. Install p7zip."; return {}; }
    if (!executeTool(tool, {"l", "-ba", archivePath}, output, err, nullptr, nullptr)) {
        error = err.empty() ? "Failed to list archive" : err;
        return {};
    }
    return parseOutput(output);
}

bool ArchiverCore::extractArchive(const std::string &archivePath, const std::string &destination,
                                   const std::string &password, std::string &error,
                                   CProgressCallback progressCallback, void *context) {
    action = CAction::Extract;
    cancelled = false;
    std::string tool = findTool();
    if (tool.empty()) { error = "No archive tool found."; return false; }
    std::vector<std::string> args = {"x", archivePath, "-o" + destination, "-y"};
    if (!password.empty()) { args.push_back("-p" + password); }
    std::string output;
    return executeTool(tool, args, output, error, progressCallback, context);
}

bool ArchiverCore::createArchive(const std::vector<std::string> &files, const std::string &destination,
                                  const std::string &format, int compressionLevel,
                                  const std::string &password, std::string &error,
                                  CProgressCallback progressCallback, void *context) {
    action = CAction::Create;
    cancelled = false;
    std::string tool = findTool();
    if (tool.empty()) { error = "No archive tool found."; return false; }
    std::vector<std::string> args = {"a", "-t" + format, destination,
                                     "-mx=" + std::to_string(compressionLevel), "-y"};
    if (!password.empty()) {
        args.push_back("-p" + password);
        if (format == "7z") args.push_back("-mhe=on");
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
    static const char *exts[] = {"7z","zip","rar","tar","gz","bz2","xz","tgz","tbz2","txz"};
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

std::string ArchiverCore::findTool() {
#if defined(__APPLE__)
    auto exists = [](const std::string &p) -> bool { return access(p.c_str(), X_OK) == 0; };
    std::vector<std::string> candidates = {
        "/opt/homebrew/bin/7z", "/usr/local/bin/7z", "/usr/bin/7z",
        "/opt/homebrew/bin/7za", "/usr/local/bin/7za",
        "/opt/homebrew/bin/7zz", "/usr/local/bin/7zz"
    };
    for (const auto &p : candidates) {
        if (exists(p)) return p;
    }
    const char *pathEnv = getenv("PATH");
    if (pathEnv) {
        std::string pathStr(pathEnv);
        std::istringstream ss(pathStr);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            std::string c = dir + "/7z";
            if (exists(c)) return c;
            c = dir + "/7za";
            if (exists(c)) return c;
        }
    }
#endif
    return "";
}

bool ArchiverCore::isToolAvailable() {
    return !findTool().empty();
}

bool ArchiverCore::executeTool(const std::string &prog, const std::vector<std::string> &args,
                                std::string &output, std::string &error,
                                CProgressCallback progressCallback, void *context) {
#if defined(__APPLE__)
    std::vector<const char *> argv;
    argv.push_back(prog.c_str());
    for (const auto &a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        error = "Failed to create pipes";
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        error = "Failed to fork process";
        return false;
    }

    if (pid == 0) {
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]); close(stderr_pipe[1]);
        execvp(prog.c_str(), const_cast<char *const *>(argv.data()));
        _exit(127);
    }

    processId = pid;
    close(stdout_pipe[1]); close(stderr_pipe[1]);

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
    std::vector<InternalEntry> entries;
    std::istringstream stream(data);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::regex splitRe(R"(\s{2,})");
        std::sregex_token_iterator it(line.begin(), line.end(), splitRe, -1);
        std::sregex_token_iterator end;
        std::vector<std::string> parts;
        for (; it != end; ++it) {
            std::string s = *it;
            if (!s.empty()) parts.push_back(s);
        }
        if (parts.empty()) continue;

        std::istringstream headStream(parts[0]);
        std::string dateStr, timeStr, attr;
        headStream >> dateStr >> timeStr >> attr;

        InternalEntry entry;
        if (!dateStr.empty() && !timeStr.empty()) {
            entry.date = dateStr + " " + timeStr;
        }
        entry.isFolder = (!attr.empty() && attr[0] == 'D');

        if (parts.size() >= 3) {
            entry.size = std::stoll(parts[1]);
            entry.name = parts.back();
            if (parts.size() >= 4) {
                entry.compressedSize = std::stoll(parts[2]);
            }
        } else if (parts.size() == 2) {
            entry.size = std::stoll(parts[1]);
        }

        if (!entry.name.empty()) {
            auto trim = [](std::string &s) {
                s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
                s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
            };
            trim(entry.name);
            entry.path = entry.name;
            entries.push_back(entry);
        }
    }
    return entries;
}
