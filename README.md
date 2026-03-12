# SMART_OPC_UA

## Установка/обновление одной командой:
```
curl -fsSL https://raw.githubusercontent.com/SnowWoolf/SMART_OPC_UA/main/um_opcua/deploy.sh | bash
```

### Конфигурационные файлы

#### config/API.cfg

сетевые настройки API
```
URL=http://localhost
LOGIN=admin
PASSWORD=admin
PROTOCOL=40
```
#### config/tags.csv

Таблица тегов
Заголовки: Тип устройства,Тип показаний,Тег API,Тег SCADA,Kind,ValueType

- Тип устройства - кодовый *номер* устройства из таблицы ПУ УМ (`"type":`)
- Тип показаний - `"measure":` в строке JSON запроса веб УМ
- Тег API - `"tag":` в строке JSON запроса веб УМ
- Тег SCADA - имя тега которое будет отображаться в дереве объектов
- Kind - Определяет, как OPC-сервер должен работать с тегом (текущие показания или исторические)
- ValueType - Определяет, какого типа данные у тега (число, строка, дата, ...)
 
---
### Пояснения к новому tags.csv


`Kind`

Определяет, как OPC-сервер должен работать с тегом.

Допустимые значения:

- `current` — текущее значение, читается обычным OPC UA Read

- `history` — исторический параметр, читается через OPC UA HistoryRead

Примеры:

- напряжение, ток, текущее время → `current`

- энергия на начало суток, энергия на начало месяца, профиль мощности → `history`

`ValueType`

Определяет, какого типа данные у тега.

Допустимые значения:

- `double` — обычное числовое значение

- `datetime` — дата и время

Примеры:

- напряжение, ток, энергия, мощность → `double`

- время счетчика (`GetTime`) → `datetime`

---
---

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
cd /root/um_opcua
rm -rf build
mkdir build
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
│   ├── tags.csv
│   └── API.cfg
└── src/
    ├── main.c
    ├── common.h
    │
    ├── api_config.c        ← новый модуль чтения /config/API.cfg
    ├── api_config.h        ← заголовок для load_api_config()
    │
    ├── api_client.c
    ├── api_client.h
    │
    ├── tag_config.c
    ├── tag_config.h
    │
    ├── opc_nodes.c
    ├── opc_nodes.h
    │
    ├── opc_history.c
    └── opc_history.h
```

### Роли модулей

##### main.c

Точка входа сервера:

- загрузка API конфигурации

- авторизация API

- загрузка списка счетчиков

- загрузка tags.csv

- запуск OPC UA сервера

- главный цикл

##### common.h

Общие структуры проекта:
```
MeterInfo
TagMapping
TagContext
ArchivePoint
ArchiveResult
AppConfig
```
И глобальные параметры API:
```
API_URL
LOGIN
PASSWORD
PROTOCOL
```
##### api_config.c

Читает файл:

`/config/API.cfg`

и заполняет:
```
API_URL
LOGIN
PASSWORD
PROTOCOL
```
##### api_client.c

Работа с API УСПД:
```
/auth
/settings/meter/table
/meter/data/moment
/meter/data/arch
```
Функции:
```
api_login()
api_get_meters()
api_read_moment()
api_read_archive()
```
##### tag_config.c

Читает:

`config/tags.csv`

И формирует таблицу маппинга:
```
Тип устройства
Тип показаний
API тег
SCADA тег
```
##### opc_nodes.c

Создает OPC UA дерево:
```
Objects
 └── Meters
      └── Meter
           └── Variables
```

И регистрирует:

`DataSource Read`
##### opc_history.c

Реализует:

`HistoryRead (ReadRaw)`

Поток:
```
OPC client
   ↓
HistoryRead
   ↓
opc_history.c
   ↓
API /meter/data/arch
   ↓
UA_DataValue[]
```