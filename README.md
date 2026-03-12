# SMART_OPC_UA
```
sudo apt update
sudo apt install -y git build-essential gcc pkg-config cmake python3 libcurl4-openssl-dev libcjson-dev
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

### Сборка:
```
cd ~/um_opcua
mkdir -p build
cd build
cmake ..
make -j"$(nproc)"
```

### Запуск:
```
cd ~/um_opcua/build
./um_opcua
```

### Пересборка и запуск:
```
cd build
cmake ..
make -j"$(nproc)"
./um_opcua
```

---

## Структура проекта

```
um_opcua/
├── CMakeLists.txt
├── config/
│   └── tags.csv
└── src/
    ├── main.c
    ├── common.h
    ├── api_client.c
    ├── api_client.h
    ├── tag_config.c
    ├── tag_config.h
    ├── opc_nodes.c
    ├── opc_nodes.h
    ├── opc_history.c
    └── opc_history.h
```

### Что делает каждый модуль

`api_client.*`

- логин в API

- GET /settings/meter/table

- POST /meter/data/moment

- POST /meter/data/arch

- нормализация ответов API в внутренние структуры


`tag_config.*`

- загрузка tags.csv

- определение типа тега:

    - current

    - day history

    - month history

- описание OPC UA data type и признака historizing


`opc_nodes.*`

- построение дерева узлов

- создание current и history variable nodes

- привязка nodeContext

- current DataSource read


`opc_history.*`

- backend для HistoryRead

- перевод HistoryReadRawModified в вызов /meter/data/arch

- сборка массива UA_DataValue


`main.c`

- инициализация

- логин

- загрузка конфигурации

- создание адресного пространства

- запуск сервера