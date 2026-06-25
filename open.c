#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>
#include <string.h>

static void usage(const wchar_t *prog)
{
    fwprintf(stderr, L"Usage: %ls [-r] path [path ...]\n", prog);
}

static wchar_t *utf8_to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) {
        return NULL;
    }

    wchar_t *ws = (wchar_t *)calloc((size_t)n, sizeof(wchar_t));
    if (!ws) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, n) <= 0) {
        free(ws);
        return NULL;
    }

    return ws;
}

static wchar_t *get_full_path(const wchar_t *path)
{
    DWORD n = GetFullPathNameW(path, 0, NULL, NULL);
    if (n == 0) {
        return NULL;
    }

    wchar_t *buf = (wchar_t *)calloc((size_t)n, sizeof(wchar_t));
    if (!buf) {
        return NULL;
    }

    if (GetFullPathNameW(path, n, buf, NULL) == 0) {
        free(buf);
        return NULL;
    }

    return buf;
}

static wchar_t *dirname_dup(const wchar_t *path)
{
    wchar_t *copy = _wcsdup(path);
    if (!copy) {
        return NULL;
    }

    size_t len = wcslen(copy);

    while (len > 0 && (copy[len - 1] == L'\\' || copy[len - 1] == L'/')) {
        copy[len - 1] = L'\0';
        len--;
    }

    wchar_t *p1 = wcsrchr(copy, L'\\');
    wchar_t *p2 = wcsrchr(copy, L'/');
    wchar_t *p = p1 > p2 ? p1 : p2;

    if (!p) {
        wcscpy(copy, L".");
        return copy;
    }

    if (p == copy) {
        p[1] = L'\0';
    } else {
        *p = L'\0';
    }

    return copy;
}

static HRESULT pidl_from_path(const wchar_t *path, PIDLIST_ABSOLUTE *pidl)
{
    SFGAOF attrs = 0;
    return SHParseDisplayName(path, NULL, pidl, 0, &attrs);
}

static int reveal_in_explorer(int argc, char **argv, int start_index)
{
    int count = argc - start_index;
    HRESULT hr;
    int ret = 0;

    wchar_t **paths = NULL;
    wchar_t **full_paths = NULL;
    wchar_t *dirpath = NULL;
    PIDLIST_ABSOLUTE dir_pidl = NULL;
    PIDLIST_ABSOLUTE *item_pidls = NULL;

    paths = (wchar_t **)calloc((size_t)count, sizeof(wchar_t *));
    full_paths = (wchar_t **)calloc((size_t)count, sizeof(wchar_t *));
    item_pidls = (PIDLIST_ABSOLUTE *)calloc((size_t)count, sizeof(PIDLIST_ABSOLUTE));

    if (!paths || !full_paths || !item_pidls) {
        fwprintf(stderr, L"out of memory\n");
        ret = 1;
        goto cleanup;
    }

    for (int i = 0; i < count; i++) {
        paths[i] = utf8_to_wide(argv[start_index + i]);
        if (!paths[i]) {
            fwprintf(stderr, L"failed to convert path to UTF-16\n");
            ret = 1;
            goto cleanup;
        }

        full_paths[i] = get_full_path(paths[i]);
        if (!full_paths[i]) {
            fwprintf(stderr, L"GetFullPathNameW failed: %lu\n", GetLastError());
            ret = 1;
            goto cleanup;
        }

        wchar_t *dir_next = dirname_dup(full_paths[i]);
        if (!dir_next) {
            fwprintf(stderr, L"out of memory\n");
            ret = 1;
            goto cleanup;
        }

        if (!dirpath) {
            dirpath = dir_next;
        } else {
            if (_wcsicmp(dirpath, dir_next) != 0) {
                fwprintf(stderr, L"different directories specified\n");
                free(dir_next);
                ret = 1;
                goto cleanup;
            }
            free(dir_next);
        }
    }

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        fwprintf(stderr, L"CoInitializeEx failed: 0x%08lx\n", (unsigned long)hr);
        ret = 1;
        goto cleanup;
    }

    hr = pidl_from_path(dirpath, &dir_pidl);
    if (FAILED(hr)) {
        fwprintf(stderr, L"SHParseDisplayName directory failed: 0x%08lx\n", (unsigned long)hr);
        ret = 1;
        goto cleanup_com;
    }

    for (int i = 0; i < count; i++) {
        hr = pidl_from_path(full_paths[i], &item_pidls[i]);
        if (FAILED(hr)) {
            fwprintf(stderr, L"SHParseDisplayName item failed: 0x%08lx\n", (unsigned long)hr);
            ret = 1;
            goto cleanup_com;
        }
    }

    hr = SHOpenFolderAndSelectItems(
        dir_pidl,
        (UINT)count,
        (PCUITEMID_CHILD_ARRAY)item_pidls,
        0
    );

    if (FAILED(hr)) {
        fwprintf(stderr, L"SHOpenFolderAndSelectItems failed: 0x%08lx\n", (unsigned long)hr);
        ret = 1;
    }

cleanup_com:
    CoUninitialize();

cleanup:
    if (item_pidls) {
        for (int i = 0; i < count; i++) {
            if (item_pidls[i]) {
                CoTaskMemFree(item_pidls[i]);
            }
        }
        free(item_pidls);
    }

    if (dir_pidl) {
        CoTaskMemFree(dir_pidl);
    }

    if (paths) {
        for (int i = 0; i < count; i++) {
            free(paths[i]);
        }
        free(paths);
    }

    if (full_paths) {
        for (int i = 0; i < count; i++) {
            free(full_paths[i]);
        }
        free(full_paths);
    }

    free(dirpath);

    return ret;
}

static int open_paths(int argc, char **argv, int start_index)
{
    int ret = 0;

    for (int i = start_index; i < argc; i++) {
        wchar_t *path = utf8_to_wide(argv[i]);
        if (!path) {
            fwprintf(stderr, L"failed to convert path to UTF-16\n");
            ret = 1;
            continue;
        }

        wchar_t *full_path = get_full_path(path);
        free(path);

        if (!full_path) {
            fwprintf(stderr, L"GetFullPathNameW failed: %lu\n", GetLastError());
            ret = 1;
            continue;
        }

        HINSTANCE h = ShellExecuteW(
            NULL,
            NULL,
            full_path,
            NULL,
            NULL,
            SW_SHOWNORMAL
        );

        if ((INT_PTR)h <= 32) {
            fwprintf(stderr, L"ShellExecuteW failed for %ls: %Id\n",
                     full_path, (INT_PTR)h);
            ret = 1;
        }

        free(full_path);
    }

    return ret;
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");

    int reveal = 0;
    int first_path = 1;

    if (first_path < argc && strcmp(argv[first_path], "-r") == 0) {
        reveal = 1;
        first_path++;
    }

    if (first_path < argc && argv[first_path][0] == '-') {
        usage(L"open");
        return 2;
    }

    if (first_path >= argc) {
        usage(L"open");
        return 2;
    }

    if (reveal) {
        return reveal_in_explorer(argc, argv, first_path);
    } else {
        return open_paths(argc, argv, first_path);
    }
}
