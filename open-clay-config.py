#!/usr/bin/env python3
"""Build, install, and open the Clay config page in one command."""

import subprocess
import sys
import os
import tempfile
import urllib.parse
import webbrowser

from pebble_tool.util import browser


original_open_config_page = browser.BrowserController.open_config_page


def patched_open_config_page(self, url, callback):
    self.port = port = self._choose_port()
    url = self.url_append_params(url, {'return_to': 'http://localhost:{}/close?'.format(port)})

    html = None

    # Clay encodes the entire config page in the hash fragment
    if '#' in url:
        fragment = url.split('#', 1)[1]
        decoded = urllib.parse.unquote(fragment)
        if decoded.startswith('<'):
            html = decoded.encode('utf-8')
    elif url.startswith('data:text/html'):
        _, _, encoded = url.partition(',')
        html = urllib.parse.unquote_to_bytes(encoded)

    if html:
        return_to = 'http://localhost:{}/close?'.format(port)
        html_str = html.decode('utf-8', errors='replace')
        html_str = html_str.replace('$$$RETURN_TO$$$', return_to)
        fd, path = tempfile.mkstemp(suffix='.html')
        with os.fdopen(fd, 'w') as f:
            f.write(html_str)
        print()
        print('Config page: file://' + path)
        print('Open this URL in your browser, then press Ctrl+C when done.')
        print()
    else:
        webbrowser.open_new(url)

    self.serve_page(port, callback)


browser.BrowserController.open_config_page = patched_open_config_page

from pebble_tool import run_tool

if __name__ == '__main__':
    project_dir = os.path.dirname(os.path.abspath(__file__))

    print('Building...')
    result = subprocess.run(['pebble', 'build'], cwd=project_dir)
    if result.returncode != 0:
        print('Build failed!')
        sys.exit(result.returncode)

    print('Installing...')
    result = subprocess.run(['pebble', 'install', '--emulator', 'emery'], cwd=project_dir)
    if result.returncode != 0:
        print('Install failed!')
        sys.exit(result.returncode)

    print('Waiting for app to start...')
    import time
    time.sleep(5)

    print('Opening config page...')
    sys.argv = ['pebble', 'emu-app-config', '--emulator', 'emery']
    run_tool()
