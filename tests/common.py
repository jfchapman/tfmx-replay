"""What the test scripts share: finding the built tools."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
BUILD_DIRS = [
    ('build', 'x64', 'Release'), ('build', 'Release'), ('build', 'win32', 'Release'),
    ('build', 'x64', 'Debug'), ('build', 'Debug'), ('build',), (),
]


def find_tool(name, env=None):
    """The tool's path: from the environment variable if it's set, otherwise the usual build folders."""
    if env and os.environ.get(env):
        return os.environ[env]
    for parts in BUILD_DIRS:
        for file in (name + '.exe', name):
            path = os.path.join(ROOT, *parts, file)
            if os.path.isfile(path):
                return path
    raise SystemExit("can't find %s: build it first, or give its path" % name)


def usage(doc, needed):
    """Prints the script's description and stops, unless it was given enough arguments."""
    if len(sys.argv) <= needed:
        raise SystemExit(doc.strip())
