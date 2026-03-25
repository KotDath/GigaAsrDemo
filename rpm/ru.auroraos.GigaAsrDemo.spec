# SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
# SPDX-License-Identifier: BSD-3-Clause

%define _cmake_skip_rpath %{nil}

%define __provides_exclude_from ^%{_datadir}/%{name}/lib/.*$
%define __requires_exclude ^(libabsl.*|libcpuinfo.*|libcrypto.*|libcurl.*|libdate.*|libflatbuffers.*|libnsync.*|libonnx.*|libonnxruntime.*|libprotobuf.*|libre2.*|libssl.*|libz.*|libatomic.*|libXNNPACK.*|libpthreadpool.*|libgomp.*|libllama.*|libggml.*|libcommon.*)$

Name:       ru.auroraos.GigaAsrDemo
Summary:    Speech-to-text demo for GigaAM
Version:    0.1
Release:    1
License:    BSD-3-Clause
URL:        https://auroraos.ru
Source0:    %{name}-%{version}.tar.bz2

Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(auroraapp)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  cmake
BuildRequires:  ninja
BuildRequires:  conan

%description
Speech-to-text demo for the bundled GigaAM ONNX models.

%prep
%autosetup

%build

CONAN_LIB_DIR="%{_builddir}/conan-libs/"
%{set_build_flags}
rm -f "$CONAN_LIB_DIR/conanrun.sh"
conan-install-if-modified --source-folder="%{_sourcedir}/.." --output-folder="$CONAN_LIB_DIR" -vwarning
PKG_CONFIG_PATH="$CONAN_LIB_DIR":$PKG_CONFIG_PATH
export PKG_CONFIG_PATH

%cmake -GNinja -DCMAKE_SYSTEM_PROCESSOR=%{_arch}
%ninja_build

%install
%ninja_install

EXECUTABLE="%{buildroot}/%{_bindir}/%{name}"
CONAN_LIB_DIR="%{_builddir}/conan-libs/"
SHARED_LIBRARIES="%{buildroot}/%{_datadir}/%{name}/lib"
mkdir -p "$SHARED_LIBRARIES"
# Temporary workaround for Conan 2.7 on x86_64: provide an ldd wrapper that
# calls /lib64/ld-linux-x86-64.so.2 --list, so conan-deploy-libraries can
# correctly resolve and copy shared libraries for the target executable.
# Newer Conan versions such as 2.9 do not need this workaround; in that case
# the plain line below is enough:
# conan-deploy-libraries "$EXECUTABLE" "$CONAN_LIB_DIR" "$SHARED_LIBRARIES"
if [ "%{_arch}" = "x86_64" ]; then
    LDD_WRAPPER_DIR="%{_builddir}/.ldd-wrapper"
    mkdir -p "$LDD_WRAPPER_DIR"
    cat > "$LDD_WRAPPER_DIR/ldd" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

rc=0
many=0
if [ "$#" -gt 1 ]; then
    many=1
fi

for f in "$@"; do
    if [ "$many" -eq 1 ]; then
        echo "${f}:"
    fi
    LD_PRELOAD= /lib64/ld-linux-x86-64.so.2 --library-path "${LD_LIBRARY_PATH:-}" --list "$f" || rc=$?
done

exit "$rc"
EOF
    chmod +x "$LDD_WRAPPER_DIR/ldd"
    export PATH="$LDD_WRAPPER_DIR:$PATH"
fi
conan-deploy-libraries "$EXECUTABLE" "$CONAN_LIB_DIR" "$SHARED_LIBRARIES"
cp -rf %{_libdir}/libgomp* "$SHARED_LIBRARIES"

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%defattr(644,root,root,-)
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
