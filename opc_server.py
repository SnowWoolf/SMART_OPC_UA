import csv
import requests
from opcua import Server, ua


# --------------------------------------------------
# CONFIG
# --------------------------------------------------

API_URL = "http://localhost"
LOGIN = "admin"
PASSWORD = "admin"

CSV_FILE = "tags.csv"

# --------------------------------------------------
# HTTP SESSION
# --------------------------------------------------

session = requests.Session()


# --------------------------------------------------
# API LOGIN
# --------------------------------------------------

def api_login():

    payload = {
        "login": LOGIN,
        "password": PASSWORD
    }

    r = session.post(API_URL + "/auth/login", json=payload)

    if r.status_code != 200:
        raise Exception("API login failed")

    print("API login success")


# --------------------------------------------------
# API WRAPPERS
# --------------------------------------------------

def api_get(url):

    r = session.get(url)

    if r.status_code == 401:

        print("Session expired -> relogin")

        api_login()

        r = session.get(url)

    return r


def api_post(url, payload):

    r = session.post(
        url,
        json=payload,
        headers={
            "Content-Type": "application/json",
            "X-Protocol-USPD": "40"
        }
    )

    if r.status_code == 401:

        print("Session expired -> relogin")

        api_login()

        r = session.post(
            url,
            json=payload,
            headers={
                "Content-Type": "application/json",
                "X-Protocol-USPD": "40"
            }
        )

    return r


# --------------------------------------------------
# API FUNCTIONS
# --------------------------------------------------

def get_meters():

    r = api_get(API_URL + "/meter/list")

    meters = r.json()

    result = []

    for m in meters:

        meter_id = m["id"]

        t = api_get(API_URL + f"/meter/{meter_id}/type")

        meter_type = t.json()["type"]

        result.append({
            "id": meter_id,
            "type": meter_type
        })

    return result


def read_api_value(meter_id, measure, tag):

    payload = {
        "ids": [meter_id],
        "measures": [measure],
        "tags": [tag]
    }

    r = api_post(
        API_URL + "/meter/data/moment",
        payload
    )

    data = r.json()

    return data["data"][0]["value"]


# --------------------------------------------------
# CSV MAPPING
# --------------------------------------------------

def load_mapping(filename):

    mapping = {}

    with open(filename, newline='', encoding='utf-8') as f:

        reader = csv.DictReader(f)

        required = ["Тип устройства", "Тег API УМ", "Тег SCADA"]

        for col in required:
            if col not in reader.fieldnames:
                raise Exception("CSV column missing: " + col)

        for row in reader:

            device_type = row["Тип устройства"]
            api_tag = row["Тег API УМ"]
            scada_tag = row["Тег SCADA"]

            mapping.setdefault(device_type, []).append({
                "api": api_tag,
                "scada": scada_tag
            })

    return mapping


# --------------------------------------------------
# OPC DATASOURCE
# --------------------------------------------------

class TagDataSource:

    def __init__(self, meter_id, measure, tag):

        self.meter_id = meter_id
        self.measure = measure
        self.tag = tag

    def read(self):

        value = read_api_value(
            self.meter_id,
            self.measure,
            self.tag
        )

        return ua.Variant(value, ua.VariantType.Float)


# --------------------------------------------------
# INIT
# --------------------------------------------------

print("Loading tag mapping")

mapping = load_mapping(CSV_FILE)

print("Starting OPC UA server")

server = Server()

server.set_endpoint("opc.tcp://0.0.0.0:4840")
server.set_server_name("UM_OPCUA")

uri = "UM.OPCUA"

idx = server.register_namespace(uri)

objects = server.get_objects_node()

meters_folder = objects.add_folder(idx, "Meters")

# --------------------------------------------------
# LOGIN API
# --------------------------------------------------

print("Login to API")

api_login()

# --------------------------------------------------
# GET METERS
# --------------------------------------------------

print("Getting meters")

meters = get_meters()

print("Meters:", meters)

# --------------------------------------------------
# BUILD OPC TREE
# --------------------------------------------------

for meter in meters:

    meter_id = meter["id"]
    meter_type = meter["type"]

    meter_node = meters_folder.add_object(idx, f"Meter_{meter_id}")

    tags = mapping.get(meter_type, [])

    for tag in tags:

        scada_tag = tag["scada"]
        api_tag = tag["api"]

        datasource = TagDataSource(
            meter_id,
            api_tag,
            scada_tag
        )

        var = meter_node.add_variable(
            idx,
            scada_tag,
            0
        )

        var.set_readable()

        var.set_value_callback(lambda node, ds=datasource: ds.read())

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
