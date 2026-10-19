"""Use activated/local ESP-IDF cJSON or the system host-test dependency."""
from pathlib import Path
import os
import shlex
import subprocess


def cjson_flags():
    root = Path(__file__).resolve().parents[1]
    idf = Path(os.environ.get('IDF_PATH', root / '.toolchains/esp-idf-v5.5.3'))
    source = idf / 'components/json/cJSON'
    if (source / 'cJSON.c').is_file():
        return ['-I' + str(source)], [str(source / 'cJSON.c')]
    cflags = shlex.split(subprocess.check_output(
        ['pkg-config', '--cflags', 'libcjson'], text=True))
    ldflags = shlex.split(subprocess.check_output(
        ['pkg-config', '--libs', 'libcjson'], text=True))
    return cflags, ldflags
