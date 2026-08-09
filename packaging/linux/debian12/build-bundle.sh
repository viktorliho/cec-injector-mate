#!/bin/bash
set -euo pipefail

readonly device_commit=55fc7e6780e35cd5e636d140049ef68e99ad6950
readonly libcec_commit=3519af0715e654c43382423d843ad658f2e01cb4
readonly p8_commit=2748be52ae27e6007ef548b697d4a03ff7de4291
readonly patch_sha=151e75f3d81578f90aca3bc343259eed381cc5e1a3f92c302c59fee12bb1980c
readonly source_epoch=${SOURCE_DATE_EPOCH:?SOURCE_DATE_EPOCH is required}

export LC_ALL=C.UTF-8 TZ=UTC ZERO_AR_DATE=1
umask 022
git config --global --add safe.directory /mate
git config --global --add safe.directory /libcec-upstream
git config --global --add safe.directory /libcec-upstream/.git
git config --global --add safe.directory /libcec-upstream/src/platform
git config --global --add safe.directory /libcec-upstream/.git/modules/src/platform
mate_commit=$(git -C /mate rev-parse HEAD)
test -z "$(git -C /mate status --porcelain)" || { echo "Mate source must be clean" >&2; exit 1; }
test "$(git -C /libcec-upstream rev-parse HEAD)" = "$libcec_commit"
test "$(git -C /libcec-upstream/src/platform rev-parse HEAD)" = "$p8_commit"
echo "$patch_sha  /mate/third_party/libcec/libcec-7.1.1-project-adapter-v1.patch" | sha256sum -c -

rm -rf /work /out/bundle
mkdir -p /work /out/bundle/bin /out/bundle/lib
git clone --quiet --no-hardlinks /libcec-upstream /work/libcec
git -C /work/libcec reset --hard "$libcec_commit"
rm -rf /work/libcec/src/platform
mkdir -p /work/libcec/src/platform
git -C /libcec-upstream/src/platform archive "$p8_commit" | tar -x -C /work/libcec/src/platform
git -C /work/libcec apply /mate/third_party/libcec/libcec-7.1.1-project-adapter-v1.patch

cmake -S /work/libcec/src/platform -B /work/p8-build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/work/prefix -DBUILD_SHARED_LIBS=ON
cmake --build /work/p8-build --parallel
cmake --install /work/p8-build
PKG_CONFIG_PATH=/work/prefix/lib/pkgconfig cmake -S /work/libcec -B /work/libcec-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/work/prefix -DBUILD_SHARED_LIBS=ON
cmake --build /work/libcec-build --parallel
cmake --install /work/libcec-build
nm -D /work/prefix/lib/libcec.so.7 \
  | grep ' cec_project_adapter_transaction_v1$' >/out/exported-symbol.txt

PKG_CONFIG_PATH=/work/prefix/lib/pkgconfig cmake -S /mate -B /work/mate-build \
  -DCMAKE_BUILD_TYPE=Release -DCEC_INJECTOR_MATE_BUILD_LINUX=ON \
  -DCEC_INJECTOR_MATE_BUILD_LIBCEC=ON
cmake --build /work/mate-build --parallel
LD_LIBRARY_PATH=/work/prefix/lib ctest --test-dir /work/mate-build --output-on-failure
install -m 0755 /work/mate-build/cec-injector-mate /out/bundle/bin/
install -m 0644 /work/prefix/lib/libcec.so.7 /out/bundle/lib/
install -m 0644 /work/prefix/lib/libp8-platform.so.2 /out/bundle/lib/
patchelf --set-rpath '$ORIGIN/../lib' /out/bundle/bin/cec-injector-mate
patchelf --set-rpath '$ORIGIN' /out/bundle/lib/libcec.so.7

exe_sha=$(sha256sum /out/bundle/bin/cec-injector-mate | cut -d' ' -f1)
cec_sha=$(sha256sum /out/bundle/lib/libcec.so.7 | cut -d' ' -f1)
p8_sha=$(sha256sum /out/bundle/lib/libp8-platform.so.2 | cut -d' ' -f1)
cat >/out/bundle/MANIFEST.txt <<EOF
CEC Injector Mate production bundle
Mate commit: $mate_commit
Device context commit: $device_commit
libCEC: 7.1.1, commit $libcec_commit
p8-platform commit: $p8_commit
Mate libCEC patch SHA-256: $patch_sha
Build userspace: Debian 12.11
Compiler: $(c++ --version | head -n1)
CMake: $(cmake --version | head -n1)
Executable SHA-256: $exe_sha
libcec.so.7 SHA-256: $cec_sha
libp8-platform.so.2 SHA-256: $p8_sha
Source date epoch: $source_epoch
Status: production-installation-candidate
EOF
tar --sort=name --mtime="@$source_epoch" --owner=0 --group=0 --numeric-owner \
  --format=posix --pax-option=delete=atime,delete=ctime -C /out -cf - bundle \
  | zstd -19 -T1 -o /out/cec-injector-mate-bundle.tar.zst
sha256sum /out/cec-injector-mate-bundle.tar.zst /out/bundle/bin/cec-injector-mate \
  /out/bundle/lib/libcec.so.7 /out/bundle/lib/libp8-platform.so.2 \
  /out/bundle/MANIFEST.txt >/out/SHA256SUMS
