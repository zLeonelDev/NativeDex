#!/bin/sh
# Builds the platform-independent runtime on Linux and runs real DEX files.
# Usage: tools/linux_harness/build_and_run.sh <Activity descriptor> [<Application descriptor>|-] [debug]
# Reads /tmp/dex/classes*.dex. Set ND_PACKAGE to the app package name if the app needs it.
# Extract the DEX files first:  python3 -c "import zipfile;z=zipfile.ZipFile('path/to/app.apk');[open('/tmp/dex/'+n,'wb').write(z.read(n)) for n in z.namelist() if n.startswith('classes') and n.endswith('.dex')]"
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=/tmp/nd_build; mkdir -p $OUT /tmp/dex $OUT/logs
INC="-I$ROOT/runtime -I$ROOT/runtime/object_model -I$ROOT/runtime/dex_loader -I$ROOT/runtime/heap"
for f in $ROOT/runtime/dex_loader/*.cpp $ROOT/runtime/object_model/*.cpp $ROOT/runtime/bytecode_interpreter/*.cpp; do
  g++ -std=c++20 -O1 -g -c $f -o $OUT/$(basename $f).o $INC -Wall -Wextra &
done; wait
g++ -std=c++20 -O1 -g $INC $ROOT/tools/linux_harness/harness.cpp $ROOT/tools/linux_harness/heapstub.cpp $OUT/*.o -o $OUT/harness -lpthread
cd $OUT && timeout 60 ./harness "$@"
