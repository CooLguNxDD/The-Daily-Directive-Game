"""Zip a packaged Windows build for itch.io: CatFleet_DirectorsCut.zip, debug symbols left out, under 1 GB.

Usage: python tools/package/make_zip.py <package dir holding Windows/> [out zip]
The package dir is what the gate's tier C archives (Saved/Gate/package). Exits 1 when the zip is 1 GB or more.
"""
import os
import sys
import zipfile

LIMIT_BYTES = 1024 ** 3
SKIP_SUFFIXES = ('.pdb',)
SKIP_NAMES = ('Manifest_DebugFiles_Win64.txt',)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = os.path.join(sys.argv[1], 'Windows')
    out = sys.argv[2] if len(sys.argv) > 2 else 'CatFleet_DirectorsCut.zip'
    if not os.path.isfile(os.path.join(root, 'Epic_AI_Mega_Jam2.exe')):
        print('make_zip: no Epic_AI_Mega_Jam2.exe under', root)
        return 2
    files = 0
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for folder, _, names in os.walk(root):
            for name in names:
                if name.endswith(SKIP_SUFFIXES) or name in SKIP_NAMES:
                    continue
                path = os.path.join(folder, name)
                z.write(path, os.path.join('DirectorsCut', os.path.relpath(path, root)))
                files += 1
    size = os.path.getsize(out)
    print('make_zip: %s files=%d size=%.1f MB limit=1024 MB %s' % (out, files, size / 1048576, 'OK' if size < LIMIT_BYTES else 'TOO BIG'))
    return 0 if size < LIMIT_BYTES else 1


if __name__ == '__main__':
    sys.exit(main())
