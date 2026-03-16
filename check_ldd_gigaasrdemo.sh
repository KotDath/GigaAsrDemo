#!/usr/bin/env bash
set -euo pipefail

~/AuroraOS/bin/sfdk engine exec -- sb2 -t AuroraOS-5.2.0.180-armv7hl.default -m sdk-install -R bash -lc "source '/home/kotdath/omp/work/qt_aurora/GigaAsrDemo/build/AuroraOS_5_2_0_180_armv7hl_in_aurora_os_build_engine_5_2_0_180_mb2_kotdath-Debug/conan-libs/conanrun.sh'; ldd '/home/deploy/installroot/usr/bin/ru.auroraos.GigaAsrDemo' 1>/tmp/ldd.out 2>/tmp/ldd.err; echo '---STDOUT---'; cat /tmp/ldd.out; echo '---STDERR---'; cat /tmp/ldd.err"
