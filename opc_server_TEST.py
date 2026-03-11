import csv
import time
import threading
import requests
from opcua import Server

API_URL = "http://localhost"
LOGIN = "admin"
PASSWORD = "admin"

PROTOCOL = "40"
CSV_FILE = "tags.csv"

UPDATE_INTERVAL = 2

session = requests.Session()


# --------------------------------------------------
# LOGIN
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

    r = session.get(API_URL + "/settings/meter/table")

    data = r.json()

    meters = data["Meters"]

    result = []

    for m in meters:

        result.append({
            "id": m["id"],
            "type": m["type"]
        })

    return result


# --------------------------------------------------
# READ VALUES
# --------------------------------------------------

def read_values(meter_id, measure, tags):

    url = API_URL + "/meter/data/moment"

    payload = {
        "ids": [meter_id],
        "measures": [measure],
        "tags": tags
    }

    headers = {
        "Content-Type": "application/json",
        "X-Protocol-USPD": PROTOCOL,
        "X-Requested-With": "XMLHttpRequest",
        "Accept": "application/json"
    }

    print("PAYLOAD:", payload)

    r = session.post(
        url,
        json=payload,
        headers=headers
    )

    print("STATUS:", r.status_code)
    print("RAW RESPONSE:", r.text)

    if r.status_code != 200:
        return {}

    data = r.json()

    result = {}

    try:

        for m in data.get("measures", []):

            for dev in m.get("devices", []):

                for v in dev.get("vals", []):

                    for t in v.get("tags", []):

                        result[t["tag"]] = t["val"]

    except Exception as e:

        print("Parse error:", e)

    return result


# --------------------------------------------------
# LOAD CSV
# --------------------------------------------------

def load_mapping(filename):

    mapping = {}

    with open(filename, newline='', encoding='utf-8-sig') as f:

        reader = csv.DictReader(f, delimiter=',')

        for row in reader:

            device_type = int(row["Тип устройства"])
            measure = row["Тег API УМ"]
            tag = row["Тег SCADA"]

            if device_type not in mapping:
                mapping[device_type] = {}

            if measure not in mapping[device_type]:
                mapping[device_type][measure] = []

            mapping[device_type][measure].append(tag)

    return mapping


# --------------------------------------------------
# UPDATE LOOP
# --------------------------------------------------

def update_loop():

    while True:

        for meter_id in meter_tags:

            measures = meter_tags[meter_id]

            for measure in measures:

                tags = measures[measure]

                try:

                    values = read_values(meter_id, measure, tags)

                    print("API values:", values)

                    for tag in tags:

                        if tag in values:

                            node = nodes[(meter_id, tag)]

                            node.set_value(float(values[tag]))

                except Exception as e:

                    print("Read error:", e)

        time.sleep(UPDATE_INTERVAL)


# --------------------------------------------------
# MAIN
# --------------------------------------------------

print("Loading tag mapping")

mapping = load_mapping(CSV_FILE)

print("Starting OPC UA server")

server = Server()
server.set_endpoint("opc.tcp://0.0.0.0:4840")

idx = server.register_namespace("UM.OPCUA")

objects = server.get_objects_node()
meters_folder = objects.add_folder(idx, "Meters")

nodes = {}
meter_tags = {}

print("Login to API")

api_login()

print("Getting meters")

meters = get_meters()

print("Meters:", meters)

for meter in meters:

    meter_id = meter["id"]
    meter_type = meter["type"]

    meter_node = meters_folder.add_object(idx, f"Meter_{meter_id}")

    measures = mapping.get(meter_type, {})

    meter_tags[meter_id] = measures

    for measure in measures:

        tags = measures[measure]

        for tag in tags:

            var = meter_node.add_variable(idx, tag, 0.0)

            nodes[(meter_id, tag)] = var


server.start()

print("OPC UA server started")

thread = threading.Thread(target=update_loop)
thread.daemon = True
thread.start()

try:

    while True:
        time.sleep(1)

except KeyboardInterrupt:

    print("Stopping server")

    server.stop()