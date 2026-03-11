# SMART_OPC_UA
```
sudo apt update
sudo apt install -y git build-essential gcc pkg-config cmake python3 libcurl4-openssl-dev
```
```
cd ~
git clone https://github.com/open62541/open62541.git
cd open62541
git submodule update --init --recursive
mkdir build
cd build
cmake -DBUILD_SHARED_LIBS=ON ..
make -j"$(nproc)"
sudo make install
sudo ldconfig
```
```
cd ~
mkdir -p um_opcua/src
cd um_opcua
загрузить файлы проекта
```
======================

# Сборка:
```
cd ~/um_opcua
mkdir -p build
cd build
cmake ..
make -j"$(nproc)"
```
======================

# Запуск:
```
cd ~/um_opcua/build
./um_opcua
```
======================

# Пересборка и запуск:
```
cd build
cmake ..
make -j"$(nproc)"
./um_opcua
```
