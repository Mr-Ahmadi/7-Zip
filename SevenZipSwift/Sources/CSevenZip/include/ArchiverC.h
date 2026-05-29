#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Types ─────────────────────────────────────────────────────────────

typedef struct {
    const char *name;
    const char *path;
    int64_t size;
    int64_t compressedSize;
    bool isFolder;
    const char *date;
} CArchiveEntry;

typedef struct {
    CArchiveEntry *entries;
    int count;
} CArchiveEntryList;

typedef void (*CProgressCallback)(int percent, void *context);

// ── Lifecycle ──────────────────────────────────────────────────────────

void *archiver_create(void);
void archiver_destroy(void *handle);
void archiver_cancel(void *handle);

// ── Operations (caller must free results) ─────────────────────────────

CArchiveEntryList *archiver_list(void *handle, const char *path, char **error);
bool archiver_extract(void *handle, const char *path, const char *dest,
                      const char *password, char **error,
                      CProgressCallback progress, void *context);
bool archiver_create_archive(void *handle, const char *const *files, int fileCount,
                     const char *dest, const char *format, int level,
                     const char *password, char **error,
                     CProgressCallback progress, void *context);

// ── Memory ─────────────────────────────────────────────────────────────

void archiver_free_entries(CArchiveEntryList *list);
void archiver_free_string(char *str);

// ── Statics ────────────────────────────────────────────────────────────

bool archiver_is_archive(const char *path);
bool archiver_is_tool_available(void);
char *archiver_find_tool(void);

#ifdef __cplusplus
}
#endif
