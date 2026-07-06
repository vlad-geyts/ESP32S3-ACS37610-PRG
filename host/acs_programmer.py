"""Portable entry point — PyInstaller builds this into ACS37610-Programmer.exe.

See build_exe.bat. Equivalent to `python -m acs_gui.app`.
"""
import sys

from acs_gui.app import main

if __name__ == "__main__":
    sys.exit(main())
