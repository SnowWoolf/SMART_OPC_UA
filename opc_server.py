import requests
import pandas as pd
from opcua import Server, ua


# -----------------------------
# CONFIG
# -----------------------------

API_URL = "http://localhost"
LOGIN = "admin"
PASSWORD = "admin"

EXCEL_FILE = "tags.xlsx"

session = requests.Session()


# -----------------------------
# API LOGIN
# -----------------------------

def api_login():

    payload = {
        "login": LOGIN,
        "password": PASSWORD
    }

    r = session.post(API_URL + "/auth/login", json=payload)

    if r.status_code != 200:
        raise Exception("API login failed")

    print("API login success")


# -----------------------------
# API WRAPPERS
# -----------------------------

def api_post(url, payload, headers=None):

    r = session.post(url, json=payload, headers=headers)

    if r.status_code == 401:

        print("Session expired -> relogin")

        api_login()

        r = session.post(url, json=payload, headers=headers)

    return r


def api_get(url):

    r = session.get(url)

    if r.status_code == 401:

        print("Session expired -> relogin")

        api_login()

        r = session.get(url)

    return r


# -----------------------------
# API FUNCTIONS
# -----------------------------

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
        payload,
        headers={
            "Content-Type": "application/json",
            "X-Protocol-USPD": "40"
        }
    )

    data = r.json()

    return data["data"][0]["value"]


# -----------------------------
# DATASOURCE
# -----------------------------

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


# -----------------------------
# LOAD EXCEL
# -----------------------------

print("Loading Excel mapping")

df = pd.read_excel(EXCEL_FILE)

mapping = {}

for _, row in df.iterrows():

    device_type = row["Тип устройства"]
    api_tag = row["Тег API УМ"]
    scada_tag = row["Тег SCADA"]

    mapping.setdefault(device_type, []).append({
        "api": api_tag,
        "scada": scada_tag
    })


# -----------------------------
# OPC SERVER
# -----------------------------

server = Server()

server.set_endpoint("opc.tcp://0.0.0.0:4840")
server.set_server_name("UM_OPCUA")

uri = "UM.OPCUA"

idx = server.register_namespace(uri)

objects = server.get_objects_node()

meters_folder = objects.add_folder(idx, "Meters")


# -----------------------------
# INIT
# -----------------------------

print("Login to API")

api_login()

print("Get meters")

meters = get_meters()

print("Meters:", meters)


# -----------------------------
# BUILD OPC TREE
# -----------------------------

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


# -----------------------------
# START
# -----------------------------

server.start()

print("OPC UA server started")

try:
    while True:
        pass

except KeyboardInterrupt:

    server.stop()
