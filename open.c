#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>

static HANDLE process_heap(void)
{
    return GetProcessHeap();
}

static void *xcalloc(SIZE_T count, SIZE_T size)
{
    if (size != 0 && count > ((SIZE_T)-1) / size) {
        return NULL;
    }
    return HeapAlloc(process_heap(), HEAP_ZERO_MEMORY, count * size);
}

static void xfree(void *ptr)
{
    if (ptr) {
        HeapFree(process_heap(), 0, ptr);
    }
}

static SIZE_T wlen(const wchar_t *s)
{
    return s ? (SIZE_T)lstrlenW(s) : 0;
}

static wchar_t *wdup(const wchar_t *s)
{
    SIZE_T len = wlen(s) + 1;
    wchar_t *copy = (wchar_t *)xcalloc(len, sizeof(wchar_t));
    if (copy) {
        for (SIZE_T i = 0; i < len; i++) {
            copy[i] = s[i];
        }
    }
    return copy;
}

static wchar_t *wfind_last(wchar_t *s, wchar_t ch)
{
    wchar_t *last = NULL;
    for (; *s; s++) {
        if (*s == ch) {
            last = s;
        }
    }
    return last;
}

static int wicmp_equal(const wchar_t *a, const wchar_t *b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

static void write_stderr(const wchar_t *s)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written;

    if (!s || h == INVALID_HANDLE_VALUE || h == NULL) {
        return;
    }

    if (!WriteConsoleW(h, s, (DWORD)wlen(s), &written, NULL)) {
        int needed = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
        if (needed > 1) {
            char *buf = (char *)xcalloc((SIZE_T)needed, sizeof(char));
            if (buf) {
                WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, needed, NULL, NULL);
                WriteFile(h, buf, (DWORD)(needed - 1), &written, NULL);
                xfree(buf);
            }
        }
    }
}

static void write_uint_dec(UINT_PTR value)
{
    wchar_t buf[32];
    int pos = 31;
    buf[pos] = L'\0';

    do {
        buf[--pos] = (wchar_t)(L'0' + (value % 10));
        value /= 10;
    } while (value != 0 && pos > 0);

    write_stderr(&buf[pos]);
}

static void write_ulong_hex(unsigned long value)
{
    static const wchar_t digits[] = L"0123456789abcdef";
    wchar_t buf[11];
    buf[0] = L'0';
    buf[1] = L'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = digits[(value >> ((7 - i) * 4)) & 0xf];
    }
    buf[10] = L'\0';
    write_stderr(buf);
}

static void usage(const wchar_t *prog)
{
    write_stderr(L"Usage: ");
    write_stderr(prog);
    write_stderr(L" [-r] path [path ...]\n");
}

static wchar_t *get_full_path(const wchar_t *path)
{
    DWORD n = GetFullPathNameW(path, 0, NULL, NULL);
    wchar_t *buf;

    if (n == 0) {
        return NULL;
    }

    buf = (wchar_t *)xcalloc((SIZE_T)n, sizeof(wchar_t));
    if (!buf) {
        return NULL;
    }

    if (GetFullPathNameW(path, n, buf, NULL) == 0) {
        xfree(buf);
        return NULL;
    }

    return buf;
}

static wchar_t *dirname_dup(const wchar_t *path)
{
    wchar_t *copy = wdup(path);
    SIZE_T len;
    wchar_t *p1;
    wchar_t *p2;
    wchar_t *p;

    if (!copy) {
        return NULL;
    }

    len = wlen(copy);

    while (len > 0 && (copy[len - 1] == L'\\' || copy[len - 1] == L'/')) {
        copy[len - 1] = L'\0';
        len--;
    }

    p1 = wfind_last(copy, L'\\');
    p2 = wfind_last(copy, L'/');
    p = p1 > p2 ? p1 : p2;

    if (!p) {
        copy[0] = L'.';
        copy[1] = L'\0';
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

static int reveal_in_explorer(int argc, wchar_t **argv, int start_index)
{
    int count = argc - start_index;
    HRESULT hr;
    int ret = 0;

    wchar_t **full_paths = NULL;
    wchar_t *dirpath = NULL;
    PIDLIST_ABSOLUTE dir_pidl = NULL;
    PIDLIST_ABSOLUTE *item_pidls = NULL;
    int com_initialized = 0;

    full_paths = (wchar_t **)xcalloc((SIZE_T)count, sizeof(wchar_t *));
    item_pidls = (PIDLIST_ABSOLUTE *)xcalloc((SIZE_T)count, sizeof(PIDLIST_ABSOLUTE));

    if (!full_paths || !item_pidls) {
        write_stderr(L"out of memory\n");
        ret = 1;
        goto cleanup;
    }

    for (int i = 0; i < count; i++) {
        wchar_t *dir_next;

        full_paths[i] = get_full_path(argv[start_index + i]);
        if (!full_paths[i]) {
            write_stderr(L"GetFullPathNameW failed: ");
            write_uint_dec(GetLastError());
            write_stderr(L"\n");
            ret = 1;
            goto cleanup;
        }

        dir_next = dirname_dup(full_paths[i]);
        if (!dir_next) {
            write_stderr(L"out of memory\n");
            ret = 1;
            goto cleanup;
        }

        if (!dirpath) {
            dirpath = dir_next;
        } else {
            if (!wicmp_equal(dirpath, dir_next)) {
                write_stderr(L"different directories specified\n");
                xfree(dir_next);
                ret = 1;
                goto cleanup;
            }
            xfree(dir_next);
        }
    }

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        write_stderr(L"CoInitializeEx failed: ");
        write_ulong_hex((unsigned long)hr);
        write_stderr(L"\n");
        ret = 1;
        goto cleanup;
    }
    com_initialized = 1;

    hr = pidl_from_path(dirpath, &dir_pidl);
    if (FAILED(hr)) {
        write_stderr(L"SHParseDisplayName directory failed: ");
        write_ulong_hex((unsigned long)hr);
        write_stderr(L"\n");
        ret = 1;
        goto cleanup;
    }

    for (int i = 0; i < count; i++) {
        hr = pidl_from_path(full_paths[i], &item_pidls[i]);
        if (FAILED(hr)) {
            write_stderr(L"SHParseDisplayName item failed: ");
            write_ulong_hex((unsigned long)hr);
            write_stderr(L"\n");
            ret = 1;
            goto cleanup;
        }
    }

    hr = SHOpenFolderAndSelectItems(
        dir_pidl,
        (UINT)count,
        (PCUITEMID_CHILD_ARRAY)item_pidls,
        0
    );

    if (FAILED(hr)) {
        write_stderr(L"SHOpenFolderAndSelectItems failed: ");
        write_ulong_hex((unsigned long)hr);
        write_stderr(L"\n");
        ret = 1;
    }

cleanup:
    if (item_pidls) {
        for (int i = 0; i < count; i++) {
            if (item_pidls[i]) {
                CoTaskMemFree(item_pidls[i]);
            }
        }
        xfree(item_pidls);
    }

    if (dir_pidl) {
        CoTaskMemFree(dir_pidl);
    }

    if (full_paths) {
        for (int i = 0; i < count; i++) {
            xfree(full_paths[i]);
        }
        xfree(full_paths);
    }

    xfree(dirpath);

    if (com_initialized) {
        CoUninitialize();
    }

    return ret;
}

static int open_paths(int argc, wchar_t **argv, int start_index)
{
    int ret = 0;

    for (int i = start_index; i < argc; i++) {
        wchar_t *full_path = get_full_path(argv[i]);

        if (!full_path) {
            write_stderr(L"GetFullPathNameW failed: ");
            write_uint_dec(GetLastError());
            write_stderr(L"\n");
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
            write_stderr(L"ShellExecuteW failed for ");
            write_stderr(full_path);
            write_stderr(L": ");
            write_uint_dec((UINT_PTR)h);
            write_stderr(L"\n");
            ret = 1;
        }

        xfree(full_path);
    }

    return ret;
}

static int app_main(void)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int reveal = 0;
    int argi = 1;
    int ret;

    if (!argv) {
        write_stderr(L"CommandLineToArgvW failed\n");
        return 1;
    }

    while (argi < argc && argv[argi][0] == L'-' && argv[argi][1] != L'\0') {
        if (argv[argi][1] == L'-' && argv[argi][2] == L'\0') {
            argi++;
            break;
        }

        if (argv[argi][1] == L'r' && argv[argi][2] == L'\0') {
            reveal = 1;
            argi++;
            continue;
        }

        usage(L"open");
        LocalFree(argv);
        return 2;
    }

    if (argi >= argc) {
        usage(L"open");
        LocalFree(argv);
        return 2;
    }

    if (reveal) {
        ret = reveal_in_explorer(argc, argv, argi);
    } else {
        ret = open_paths(argc, argv, argi);
    }

    LocalFree(argv);
    return ret;
}

void WINAPI wWinMainCRTStartup(void)
{
    ExitProcess((UINT)app_main());
}
