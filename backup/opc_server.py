import csv
import requests
from opcua import Server, ua
from datetime import datetime

API_URL = "http://localhost"
LOGIN = "admin"
PASSWORD = "admin"

PROTOCOL = "40"
CSV_FILE = "tags.csv"

session = requests.Session()


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
        headers={
            "X-Protocol-USPD": "40"
        }
    )
    
    print("Status:", r.status_code)
    print("Headers:", r.headers)
    print("Text:", r.text)
    data = r.json()
    print("Meters response:", data)
    meters = []

    for m in data.get("Meters", []):

        meter = {
            "id": m.get("id"),
            "type": m.get("type"),
            "typeName": m.get("typeName", "Meter"),
            "addr": m.get("addr", "")
        }

        meters.append(meter)

    return meters


# --------------------------------------------------
# API READ
# --------------------------------------------------

def read_values(meter_id, measure, tags):

    url = API_URL + "/meter/data/moment"

    payload = {
        "ids": [meter_id],
        "measures": [measure]
    }
    if measure != "GetTime":
        payload["tags"] = tags
    else:
        payload["tags"] = []

    headers = {
        "Content-Type": "application/json",
        "X-Protocol-USPD": PROTOCOL,
        "X-Requested-With": "XMLHttpRequest",
        "Accept": "application/json"
    }

    print("API REQUEST:", payload)

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

                    for tag in vals.get("tags", []):
                        result[tag["tag"]] = tag["val"]

    except Exception as e:
        print("Parse error:", e)

    return result


# --------------------------------------------------
# LOAD CSV
# --------------------------------------------------

def load_mapping(filename):

    mapping = {}

    with open(filename, newline='', encoding='utf-8-sig') as f:

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
# SERVER INIT
# --------------------------------------------------

print("Loading tag mapping")

mapping = load_mapping(CSV_FILE)

print("Starting OPC UA server")

server = Server()

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

print("Meters:", meters)

node_map = {}

for meter in meters:

    meter_id = meter["id"]
    meter_type = meter["type"]
    name = f'{meter["typeName"]}_{meter["addr"]}'
    meter_node = meters_folder.add_object(idx, name)

    measures = mapping.get(meter_type, {})

    for measure in measures:

        for item in measures[measure]:

            api_tag = item["api_tag"]
            display = item["display"]

            var = meter_node.add_variable(idx, display, 0.0)

            node_map[var.nodeid] = (meter_id, measure, api_tag)


# --------------------------------------------------
# PATCH READ FUNCTION
# --------------------------------------------------

aspace = server.iserver.aspace
original_get_attribute_value = aspace.get_attribute_value


def custom_get_attribute_value(nodeid, attr):

    # перехватываем только чтение Value
    if attr == ua.AttributeIds.Value and nodeid in node_map:

        meter_id, measure, tag = node_map[nodeid]

        values = read_values(meter_id, measure, [tag])
        # если API ничего не вернул (например счётчик удалён)
        if not values:
            return ua.DataValue(
                ua.Variant(None),
                status=ua.StatusCode(ua.StatusCodes.BadNoData)
            )
        if measure == "GetTime":

            if "time" in values:
                value = datetime.fromtimestamp(values["time"])

                dv = ua.DataValue(
                    ua.Variant(value, ua.VariantType.DateTime)
                )

                return dv

            return ua.DataValue(
                ua.Variant(None, ua.VariantType.DateTime)
            )

        value = 0.0

        if tag in values:
            value = float(values[tag])
            var = server.get_node(nodeid)
            var.set_value(value)

        dv = ua.DataValue(
            ua.Variant(value, ua.VariantType.Double)
        )
        dv.SourceTimestamp = datetime.utcnow()
        dv.ServerTimestamp = datetime.utcnow()

        return dv

    return original_get_attribute_value(nodeid, attr)


aspace.get_attribute_value = custom_get_attribute_value


# --------------------------------------------------
# START SERVER
# --------------------------------------------------

server.start()

print("OPC UA server started")

try:

    while True:
        pass

except KeyboardInterrupt:

    print("Stopping server")

    server.stop()