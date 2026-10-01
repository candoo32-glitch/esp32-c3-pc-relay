Import("env")

import os

version = os.environ.get("FW_BUILD_VERSION", "0")
try:
    int(version)
except ValueError:
    version = "0"

env.Append(CPPDEFINES=[("FW_BUILD_VERSION", version)])
