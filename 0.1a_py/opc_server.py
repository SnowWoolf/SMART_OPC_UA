# --------------------------------------------------
# OPC UA API Proxy Server
# --------------------------------------------------

import csv
import requests
import time

from datetime import datetime
from opcua import Server, ua
from opcua.server.history import HistoryStorageInterface


API_URL = "http://localhost"
LOGIN = "admin"
PASSWORD = "admin"

PROTOCOL = "40"
CSV_FILE = "tags.csv"

session = requests.Session()

node_map = {}
history_nodes = []


# --------------------------------------------------
# API LOGIN
# --------------------------------------------------

def api_login():

    payload = {
        "login": LOGIN,
        "password": PASSWORD
    }

    r = session.post(
        API_URL + "/auth",
        json=payload,
        headers={"Content-Type": "application/json"}
    )

    print("Login status:", r.status_code)

    if r.status_code != 200:
        raise Exception("API login failed")


# --------------------------------------------------
# GET METERS
# --------------------------------------------------

def get_meters():

    r = session.get(
        API_URL + "/settings/meter/table",
        headers={"X-Protocol-USPD": PROTOCOL}
    )

    print("Status:", r.status_code)

    data = r.json()

    meters = []

    for m in data.get("Meters", []):

        meters.append({
            "id": m.get("id"),
            "type": m.get("type"),
            "typeName": m.get("typeName", "Meter"),
            "addr": m.get("addr", "")
        })

    return meters


# --------------------------------------------------
# API MOMENT READ
# --------------------------------------------------

def parse_api_time(raw_value):
    if raw_value is None:
        return None

    if isinstance(raw_value, (int, float)):
        return datetime.fromtimestamp(raw_value)

    if isinstance(raw_value, str):
        try:
            if raw_value.endswith("Z"):
                raw_value = raw_value.replace("Z", "+00:00")
            return datetime.fromisoformat(raw_value)
        except Exception:
            return None

    return None


def read_values(meter_id, measure, tags):

    url = API_URL + "/meter/data/moment"

    payload = {
        "ids": [meter_id],
        "measures": [measure],
        "tags": tags
    }

    headers = {
        "Content-Type": "application/json",
        "X-Protocol-USPD": PROTOCOL
    }

    r = session.post(url, json=payload, headers=headers)

    if r.status_code != 200:
        print("API error:", r.status_code)
        return {}

    data = r.json()
    result = {}

    try:

        for m in data.get("measures", []):

            for dev in m.get("devices", []):

                for vals in dev.get("vals", []):

                    # обычные теги
                    for tag in vals.get("tags", []):
                        try:
                            result[tag["tag"]] = float(tag["val"])
                        except Exception:
                            result[tag["tag"]] = tag["val"]

                    # специальный случай для GetTime
                    ts = vals.get("ts")
                    if ts is not None:
                        parsed = parse_api_time(ts)
                        if parsed is not None:
                            result["__time__"] = parsed

                    raw_time = vals.get("time")
                    if raw_time is not None and "__time__" not in result:
                        parsed = parse_api_time(raw_time)
                        if parsed is not None:
                            result["__time__"] = parsed

    except Exception as e:
        print("Parse error:", e)

    return result


# --------------------------------------------------
# API ARCHIVE READ
# --------------------------------------------------

def read_archive(meter_id, measure, tag, start, end):

    if start is None:
        start = datetime(1970, 1, 1)

    if end is None:
        end = datetime.utcnow()

    url = API_URL + "/meter/data/arch"

    payload = {
        "ids": [meter_id],
        "measures": [measure],
        "tags": [tag],
        "time": {
            "start": int(start.timestamp()),
            "end": int(end.timestamp())
        }
    }

    headers = {
        "Content-Type": "application/json",
        "X-Protocol-USPD": PROTOCOL
    }

    r = session.post(url, json=payload, headers=headers)

    if r.status_code != 200:
        print("Archive API error:", r.status_code)
        return []

    data = r.json()
    result = []

    try:

        for m in data.get("measures", []):

            for dev in m.get("devices", []):

                for vals in dev.get("vals", []):

                    ts = vals.get("ts")
                    if ts is None:
                        continue

                    if isinstance(ts, str) and ts.endswith("Z"):
                        ts = ts.replace("Z", "+00:00")

                    timestamp = datetime.fromisoformat(ts)

                    for t in vals.get("tags", []):

                        if t["tag"] == tag:
                            value = float(t["val"])
                            result.append((timestamp, value))

    except Exception as e:
        print("Archive parse error:", e)

    return result


# --------------------------------------------------
# CSV TAG MAPPING
# --------------------------------------------------

def load_mapping(filename):

    mapping = {}

    with open(filename, newline="", encoding="utf-8-sig") as f:

        reader = csv.DictReader(f)

        for row in reader:

            device_type = int(row["Тип устройства"])
            measure = row["Тип показаний"]
            api_tag = row["Тег API"]
            display = row["Тег SCADA"]

            mapping.setdefault(device_type, {})
            mapping[device_type].setdefault(measure, [])

            mapping[device_type][measure].append({
                "api_tag": api_tag,
                "display": display
            })

    return mapping


# --------------------------------------------------
# HISTORY STORAGE
# --------------------------------------------------

class ApiHistoryStorage(HistoryStorageInterface):

    def new_historized_node(self, node_id, period, count):
        # ничего не сохраняем локально
        pass

    def save_node_value(self, node_id, datavalue):
        # ничего не сохраняем локально
        pass

    def read_node_history(self, node_id, start, end, num_values):

        node_key = node_id.to_string()

        print("History request:", node_key, start, end)

        if node_key not in node_map:
            return []

        info = node_map[node_key]

        meter_id = info["meter_id"]
        measure = info["measure"]
        tag = info["tag"]
        variant_type = info["variant_type"]

        # для GetTime историю не читаем
        if measure == "GetTime" or variant_type != ua.VariantType.Double:
            return []

        values = read_archive(meter_id, measure, tag, start, end)

        dv_list = []

        for ts, val in values:

            dv = ua.DataValue(
                ua.Variant(val, ua.VariantType.Double)
            )

            dv.SourceTimestamp = ts
            dv.ServerTimestamp = ts
            dv.StatusCode = ua.StatusCode(ua.StatusCodes.Good)

            dv_list.append(dv)

        return dv_list

    def stop(self):
        pass


# --------------------------------------------------
# SERVER INIT
# --------------------------------------------------

print("Loading tag mapping")
mapping = load_mapping(CSV_FILE)

print("Starting OPC UA server")
server = Server()

history_storage = ApiHistoryStorage()
server.iserver.history_manager.set_storage(history_storage)
server.iserver.history_manager._enabled = True

server.set_endpoint("opc.tcp://0.0.0.0:4840")
server.set_server_name("UM_SMART_OPCUA")

uri = "urn:um-smart-opcua"
idx = server.register_namespace(uri)

objects = server.get_objects_node()
meters_folder = objects.add_folder(idx, "Meters")

print("Login to API")
api_login()

print("Getting meters")
meters = get_meters()
print("Meters count:", len(meters))


# --------------------------------------------------
# CREATE OPC NODES
# --------------------------------------------------

for meter in meters:

    meter_id = meter["id"]
    meter_type = meter["type"]
    meter_name = f'{meter["typeName"]}_{meter["addr"]}'

    meter_node = meters_folder.add_object(idx, meter_name)

    measures = mapping.get(meter_type, {})

    for measure in measures:

        for item in measures[measure]:

            api_tag = item["api_tag"]
            display = item["display"]

            safe_tag = api_tag if api_tag else "no_tag"

            nodeid = ua.NodeId(
                f"{measure}_{safe_tag}_{meter_id}",
                idx,
                ua.NodeIdType.String
            )

            if measure == "GetTime":
                initial_variant = ua.Variant(datetime.utcnow(), ua.VariantType.DateTime)
                variant_type = ua.VariantType.DateTime
                historizing = False
                access_mask = int(ua.AccessLevel.CurrentRead)
            else:
                initial_variant = ua.Variant(0.0, ua.VariantType.Double)
                variant_type = ua.VariantType.Double
                historizing = True
                access_mask = int(ua.AccessLevel.CurrentRead | ua.AccessLevel.HistoryRead)

            var = meter_node.add_variable(
                nodeid,
                display,
                initial_variant
            )

            var.set_attribute(
                ua.AttributeIds.AccessLevel,
                ua.DataValue(ua.Variant(access_mask, ua.VariantType.Byte))
            )

            var.set_attribute(
                ua.AttributeIds.UserAccessLevel,
                ua.DataValue(ua.Variant(access_mask, ua.VariantType.Byte))
            )

            var.set_attribute(
                ua.AttributeIds.Historizing,
                ua.DataValue(ua.Variant(historizing, ua.VariantType.Boolean))
            )

            node_map[var.nodeid.to_string()] = {
                "meter_id": meter_id,
                "measure": measure,
                "tag": api_tag,
                "variant_type": variant_type
            }

            if historizing:
                history_nodes.append(var)


# --------------------------------------------------
# PATCH READ
# --------------------------------------------------

aspace = server.iserver.aspace
original_get_attribute_value = aspace.get_attribute_value


def custom_get_attribute_value(nodeid, attr):

    node_key = nodeid.to_string()

    if attr == ua.AttributeIds.Value and node_key in node_map:

        info = node_map[node_key]

        meter_id = info["meter_id"]
        measure = info["measure"]
        tag = info["tag"]
        variant_type = info["variant_type"]

        try:

            if measure == "GetTime":
                values = read_values(meter_id, measure, [])
                if "__time__" in values:
                    value = values["__time__"]
                    status = ua.StatusCode(ua.StatusCodes.Good)
                else:
                    value = datetime.utcnow()
                    status = ua.StatusCode(ua.StatusCodes.BadNoData)

                dv = ua.DataValue(
                    ua.Variant(value, ua.VariantType.DateTime)
                )

            else:
                values = read_values(meter_id, measure, [tag])

                if tag in values:
                    value = float(values[tag])
                    status = ua.StatusCode(ua.StatusCodes.Good)
                else:
                    value = 0.0
                    status = ua.StatusCode(ua.StatusCodes.BadNoData)

                dv = ua.DataValue(
                    ua.Variant(value, variant_type)
                )

        except Exception as e:

            print("Read error:", e)

            if variant_type == ua.VariantType.DateTime:
                dv = ua.DataValue(
                    ua.Variant(datetime.utcnow(), ua.VariantType.DateTime)
                )
            else:
                dv = ua.DataValue(
                    ua.Variant(0.0, ua.VariantType.Double)
                )

            status = ua.StatusCode(ua.StatusCodes.Bad)

        now = datetime.utcnow()

        dv.SourceTimestamp = now
        dv.ServerTimestamp = now
        dv.StatusCode = status

        return dv

    return original_get_attribute_value(nodeid, attr)


aspace.get_attribute_value = custom_get_attribute_value


# --------------------------------------------------
# START SERVER
# --------------------------------------------------

server.start()
print("OPC UA server started")

print("Enabling history")
for node in history_nodes:
    server.iserver.history_manager.historize_data_change(node)

try:
    while True:
        time.sleep(1)

except KeyboardInterrupt:
    print("Stopping server")
    server.stop()