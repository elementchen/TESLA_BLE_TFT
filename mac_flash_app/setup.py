"""
py2app setup for ESP32 Config Tool macOS .app bundle.
Build:  python3.12 setup.py py2app
"""
from setuptools import setup

APP = ['esp32_config_gui.py']
DATA_FILES = []
OPTIONS = {
    'argv_emulation': False,
    'packages': ['serial', 'tkinter'],
    'includes': ['serial.tools.list_ports', 'tkinter.ttk', 'tkinter.messagebox'],
    'plist': {
        'CFBundleName': 'ESP32 Config Tool',
        'CFBundleShortVersionString': '1.0.0',
        'CFBundleIdentifier': 'com.tesla.esp32config',
        'CFBundleDocumentTypes': [],
        'NSHighResolutionCapable': True,
        'LSMinimumSystemVersion': '12.0',
    },
}

setup(
    app=APP,
    data_files=DATA_FILES,
    options={'py2app': OPTIONS},
    setup_requires=['py2app'],
)
