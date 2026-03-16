#!/usr/bin/env bash
set -euo pipefail

PKG_NAME="um-opcua"
PKG_VERSION="${PKG_VERSION:-1.0.0}"
PKG_RELEASE="${PKG_RELEASE:-1}"
ARCH="$(dpkg --print-architecture)"

REPO_URL="https://github.com/SnowWoolf/SMART_OPC_UA.git"
REPO_BRANCH="main"

WORK_ROOT="/tmp/${PKG_NAME}-build"
SRC_ROOT="${WORK_ROOT}/src"
REPO_DIR="${SRC_ROOT}/repo"
PROJECT_DIR="${REPO_DIR}/um_opcua"
BUILD_DIR="${PROJECT_DIR}/build"

PKG_ROOT="${WORK_ROOT}/pkgroot"
DEBIAN_DIR="${PKG_ROOT}/DEBIAN"

INSTALL_PREFIX="/opt/um_opcua"
INSTALL_BIN_DIR="${INSTALL_PREFIX}/build"
INSTALL_LIB_DIR="${INSTALL_PREFIX}/lib"
INSTALL_CONFIG_LINK="${INSTALL_PREFIX}/config"

ETC_CONFIG_DIR="/etc/um_opcua"

SERVICE_NAME="um_opcua"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

OUTPUT_DEB="$(pwd)/${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.deb"

OPEN62541_VERSION="v1.5.2"

echo "==> Cleanup"
rm -rf "${WORK_ROOT}"
mkdir -p "${SRC_ROOT}" "${DEBIAN_DIR}"

echo "==> Installing build dependencies"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
    git \
    curl \
    cmake \
    build-essential \
    pkg-config \
    libcjson-dev \
    libcurl4-openssl-dev

echo "==> Installing open62541 if missing"
if [ ! -f /usr/local/lib/cmake/open62541/open62541Config.cmake ] && \
   [ ! -f /usr/lib/cmake/open62541/open62541Config.cmake ]; then

    TMP_DIR="/tmp/open62541-build"
    rm -rf "${TMP_DIR}"
    mkdir -p "${TMP_DIR}"

    git clone --branch "${OPEN62541_VERSION}" --depth 1 \
        https://github.com/open62541/open62541.git \
        "${TMP_DIR}/src"

    mkdir -p "${TMP_DIR}/src/build"
    cd "${TMP_DIR}/src/build"

    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DUA_ENABLE_HISTORIZING=ON \
        -DBUILD_SHARED_LIBS=ON

    make -j"$(nproc)"
    make install
    ldconfig || true
    cd /
    rm -rf "${TMP_DIR}"
else
    echo "==> open62541 already installed"
fi

echo "==> Cloning/updating repository"
if [ ! -d "${REPO_DIR}/.git" ]; then
    git clone --branch "${REPO_BRANCH}" "${REPO_URL}" "${REPO_DIR}"
else
    git -C "${REPO_DIR}" fetch --all --tags
    git -C "${REPO_DIR}" reset --hard "origin/${REPO_BRANCH}"
fi

echo "==> Building project"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake ..
make -j"$(nproc)"

BIN="${BUILD_DIR}/um_opcua"
if [ ! -f "${BIN}" ]; then
    echo "ERROR: binary not found: ${BIN}"
    exit 1
fi

echo "==> Preparing package filesystem"
mkdir -p "${PKG_ROOT}${INSTALL_BIN_DIR}"
mkdir -p "${PKG_ROOT}${INSTALL_LIB_DIR}"
mkdir -p "${PKG_ROOT}${ETC_CONFIG_DIR}"
mkdir -p "${PKG_ROOT}/opt"
mkdir -p "${PKG_ROOT}/etc/systemd/system"

echo "==> Installing binary"
install -m 0755 "${BIN}" "${PKG_ROOT}${INSTALL_BIN_DIR}/um_opcua"

echo "==> Installing default configs"
if [ -f "${PROJECT_DIR}/config/tags.csv" ]; then
    install -m 0644 "${PROJECT_DIR}/config/tags.csv" "${PKG_ROOT}${ETC_CONFIG_DIR}/tags.csv"
else
    cat > "${PKG_ROOT}${ETC_CONFIG_DIR}/tags.csv" <<'EOF'
Тип устройства,Тип показаний,Тег API,Тег SCADA,Kind,ValueType
EOF
fi

cat > "${PKG_ROOT}${ETC_CONFIG_DIR}/API.cfg" <<'EOF'
URL=http://localhost
LOGIN=admin
PASSWORD=admin
PROTOCOL=40
EOF

echo "==> Creating config symlink"
ln -sfn /etc/um_opcua "${PKG_ROOT}${INSTALL_CONFIG_LINK}"

echo "==> Collecting bundled runtime libraries"

copy_one_with_symlinks() {
    local source="$1"
    local resolved
    local dir

    [ -e "$source" ] || return 0

    resolved="$(readlink -f "$source")"
    dir="$(dirname "$source")"

    cp -a "$resolved" "${PKG_ROOT}${INSTALL_LIB_DIR}/"

    find "$dir" -maxdepth 1 -type l | while read -r link; do
        local target
        target="$(readlink -f "$link")"
        if [ "$target" = "$resolved" ]; then
            cp -a "$link" "${PKG_ROOT}${INSTALL_LIB_DIR}/"
        fi
    done
}

copy_matching_libs() {
    local found=1
    for pattern in "$@"; do
        for f in $pattern; do
            [ -e "$f" ] || continue
            echo "    + $f"
            copy_one_with_symlinks "$f"
            found=0
        done
    done
    return $found
}

OPEN62541_FOUND=0
CJSON_FOUND=0

if copy_matching_libs \
    /usr/local/lib/libopen62541.so* \
    /usr/lib/libopen62541.so* \
    /usr/lib/arm-linux-gnueabihf/libopen62541.so* \
    /lib/arm-linux-gnueabihf/libopen62541.so*; then
    OPEN62541_FOUND=1
fi

if copy_matching_libs \
    /usr/local/lib/libcjson.so* \
    /usr/lib/libcjson.so* \
    /usr/lib/arm-linux-gnueabihf/libcjson.so* \
    /lib/arm-linux-gnueabihf/libcjson.so*; then
    CJSON_FOUND=1
fi

if [ "${OPEN62541_FOUND}" -ne 1 ]; then
    echo "ERROR: libopen62541.so* not found"
    exit 1
fi

if [ "${CJSON_FOUND}" -ne 1 ]; then
    echo "ERROR: libcjson.so* not found"
    exit 1
fi

echo "==> Creating systemd unit"
cat > "${PKG_ROOT}${SERVICE_FILE}" <<EOF
[Unit]
Description=SMART OPC UA Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=${INSTALL_BIN_DIR}
Environment=LD_LIBRARY_PATH=${INSTALL_LIB_DIR}
ExecStart=${INSTALL_BIN_DIR}/um_opcua
Restart=always
RestartSec=3
User=root

[Install]
WantedBy=multi-user.target
EOF

echo "==> Creating control file"
INSTALLED_SIZE="$(du -sk "${PKG_ROOT}" | awk '{print $1}')"

cat > "${DEBIAN_DIR}/control" <<EOF
Package: ${PKG_NAME}
Version: ${PKG_VERSION}-${PKG_RELEASE}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: Valery <valery@example.local>
Depends: libc6, libgcc-s1, libstdc++6, libcurl4
Description: SMART OPC UA Server
 OPC UA server for SMART / UM platform.
Installed-Size: ${INSTALLED_SIZE}
EOF

echo "==> Marking config files as conffiles"
cat > "${DEBIAN_DIR}/conffiles" <<EOF
${ETC_CONFIG_DIR}/API.cfg
${ETC_CONFIG_DIR}/tags.csv
EOF

echo "==> Creating postinst"
cat > "${DEBIAN_DIR}/postinst" <<'EOF'
#!/usr/bin/env bash
set -e

mkdir -p /opt/um_opcua/build
mkdir -p /opt/um_opcua/lib
mkdir -p /etc/um_opcua

if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
    systemctl enable um_opcua.service || true
    systemctl restart um_opcua.service || true
fi

exit 0
EOF
chmod 0755 "${DEBIAN_DIR}/postinst"

echo "==> Creating prerm"
cat > "${DEBIAN_DIR}/prerm" <<'EOF'
#!/usr/bin/env bash
set -e

if command -v systemctl >/dev/null 2>&1; then
    systemctl stop um_opcua.service || true
    systemctl disable um_opcua.service || true
fi

exit 0
EOF
chmod 0755 "${DEBIAN_DIR}/prerm"

echo "==> Creating postrm"
cat > "${DEBIAN_DIR}/postrm" <<'EOF'
#!/usr/bin/env bash
set -e

if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
fi

exit 0
EOF
chmod 0755 "${DEBIAN_DIR}/postrm"

echo "==> Setting permissions"
find "${PKG_ROOT}" -type d -exec chmod 0755 {} \;
find "${PKG_ROOT}" -type f -exec chmod 0644 {} \;

chmod 0755 "${PKG_ROOT}${INSTALL_BIN_DIR}/um_opcua"
chmod 0755 "${PKG_ROOT}${SERVICE_FILE}"
chmod 0755 "${DEBIAN_DIR}/postinst" "${DEBIAN_DIR}/prerm" "${DEBIAN_DIR}/postrm"

echo "==> Package contents overview"
find "${PKG_ROOT}" -maxdepth 4 | sort

echo "==> Building deb package"
dpkg-deb --build "${PKG_ROOT}" "${OUTPUT_DEB}"

echo
echo "==> DONE"
echo "Package created:"
echo "    ${OUTPUT_DEB}"
echo
echo "Install with:"
echo "    dpkg -i ${OUTPUT_DEB}"
echo
echo "Inspect package:"
echo "    dpkg-deb -I ${OUTPUT_DEB}"
echo "    dpkg-deb -c ${OUTPUT_DEB}"
echo
echo "Check runtime linkage:"
echo "    LD_LIBRARY_PATH=${INSTALL_LIB_DIR} ldd ${INSTALL_BIN_DIR}/um_opcua"
echo
echo "Check service:"
echo "    systemctl status ${SERVICE_NAME} --no-pager"