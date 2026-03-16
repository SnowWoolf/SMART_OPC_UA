#!/usr/bin/env bash
set -euo pipefail

REPO_URL="https://github.com/SnowWoolf/SMART_OPC_UA.git"
REPO_BRANCH="main"

INSTALL_DIR="/opt/um_opcua"
REPO_DIR="${INSTALL_DIR}/repo"
BUILD_DIR="${REPO_DIR}/um_opcua/build"
PROJECT_DIR="${REPO_DIR}/um_opcua"

CONFIG_DIR="${INSTALL_DIR}/config"
DATA_TAGS="${CONFIG_DIR}/tags.csv"
DATA_API_CFG="${CONFIG_DIR}/API.cfg"

SERVICE_NAME="um_opcua"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

echo "==> Installing dependencies"
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
	
echo "==> Installing open62541"

OPEN62541_VERSION="v1.5.2"

if [ ! -f /usr/local/lib/cmake/open62541/open62541Config.cmake ] && \
   [ ! -f /usr/lib/cmake/open62541/open62541Config.cmake ]; then

    TMP_DIR="/tmp/open62541-build"
    rm -rf "${TMP_DIR}"
    mkdir -p "${TMP_DIR}"

    git clone --branch "${OPEN62541_VERSION}" --depth 1 https://github.com/open62541/open62541.git "${TMP_DIR}/src"

    mkdir -p "${TMP_DIR}/src/build"
    cd "${TMP_DIR}/src/build"

    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DUA_ENABLE_HISTORIZING=ON \
        -DBUILD_SHARED_LIBS=ON

    make -j"$(nproc)"
    make install
    ldconfig
	
    cd /
    rm -rf "${TMP_DIR}"
else
    echo "==> open62541 already installed"
fi

echo "==> Preparing directories"
mkdir -p "${INSTALL_DIR}"
mkdir -p "${CONFIG_DIR}"

if [ ! -d "${REPO_DIR}/.git" ]; then
    echo "==> Cloning repository"
    git clone --branch "${REPO_BRANCH}" "${REPO_URL}" "${REPO_DIR}"
else
    echo "==> Updating repository"
    git -C "${REPO_DIR}" fetch --all --tags
    git -C "${REPO_DIR}" reset --hard "origin/${REPO_BRANCH}"
fi

echo "==> Preserving user config files if they already exist"

if [ ! -f "${DATA_TAGS}" ]; then
    if [ -f "${PROJECT_DIR}/config/tags.csv" ]; then
        cp "${PROJECT_DIR}/config/tags.csv" "${DATA_TAGS}"
    else
        echo "Тип устройства,Тип показаний,Тег API,Тег SCADA,Kind,ValueType" > "${DATA_TAGS}"
    fi
fi

if [ ! -f "${DATA_API_CFG}" ]; then
    cat > "${DATA_API_CFG}" <<'EOF'
URL=http://localhost
LOGIN=admin
PASSWORD=admin
PROTOCOL=40
EOF
fi

echo "==> Linking config files into project tree"
mkdir -p "${PROJECT_DIR}/config"

rm -f "${PROJECT_DIR}/config/tags.csv"
ln -s "${DATA_TAGS}" "${PROJECT_DIR}/config/tags.csv"

rm -f "${PROJECT_DIR}/config/API.cfg"
ln -s "${DATA_API_CFG}" "${PROJECT_DIR}/config/API.cfg"

echo "==> Building project"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake ..
make -j"$(nproc)"

echo "==> Installing systemd service"
cat > "${SERVICE_FILE}" <<EOF
[Unit]
Description=SMART OPC UA Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=${BUILD_DIR}
ExecStart=${BUILD_DIR}/um_opcua
Restart=always
RestartSec=3
User=root

[Install]
WantedBy=multi-user.target
EOF

echo "==> Enabling and starting service"
systemctl daemon-reload
systemctl enable "${SERVICE_NAME}"
systemctl restart "${SERVICE_NAME}"

echo
echo "==> Service status"
systemctl status "${SERVICE_NAME}" --no-pager
