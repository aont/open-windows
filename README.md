# open-windows

`open-windows` is a small Windows command-line utility for opening files or folders with their default application. It can also reveal one or more items in File Explorer.

## Features

- Opens files and directories through the Windows shell.
- Supports Unicode command-line paths through the native Windows UTF-16 command line.
- Accepts multiple paths in one invocation.
- Reveals selected items in File Explorer with the `-r` option.

## Requirements

- Windows
- MinGW-w64 or another GCC toolchain that provides the Windows SDK headers and libraries
- `make`

The executable is built without the C runtime: the MinGW build passes `-nostartfiles -nodefaultlibs`, and the MSVC build passes `/NODEFAULTLIB`.

## Build

Use the included MinGW makefile:

```sh
make -f Makefile.mingw
```

This builds `open.exe` in the repository root.

To remove build outputs:

```sh
make -f Makefile.mingw clean
```

## Usage

```text
open.exe [-r] path [path ...]
```

Open files or folders with their default Windows application:

```sh
open.exe document.txt C:\Users\Example\Downloads
```

Reveal files in File Explorer:

```sh
open.exe -r C:\Users\Example\Downloads\document.txt
```

When using `-r` with multiple items, all items must be in the same directory:

```sh
open.exe -r C:\Temp\a.txt C:\Temp\b.txt
```

## Exit codes

- `0`: all requested operations succeeded
- `1`: at least one operation failed
- `2`: invalid command-line usage

## Notes

- Paths are resolved to full paths before they are passed to the Windows shell.
- The reveal mode uses `SHOpenFolderAndSelectItems`, so it is intended for files or folders that can be parsed by the Windows shell namespace.
