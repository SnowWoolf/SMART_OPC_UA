import time
import requests
from opcua import Server, ua

API_URL = "http://localhost/meter/data/moment"

DEVICE_ID = 2
MEASURE = "ElMomentEnergy"
TAG = "A+0"


def read_from_api():
    payload = {
        "ids": [DEVICE_ID],
        "measures": [MEASURE],
        "tags": [TAG]
    }

    headers = {
        "Content-Type": "application/json",
        "X-Protocol-USPD": "40"
    }

    r = requests.post(API_URL, json=payload, headers=headers, timeout=10)
    data = r.json()

    # структура может отличаться — подправим позже
    value = data["measures"][0]["devices"][0]["tags"][TAG]

    return float(value)


class ApiVariable:

    def read(self):
        try:
            value = read_from_api()
            return ua.Variant(value, ua.VariantType.Double)
        except Exception as e:
            print("API error:", e)
            return ua.Variant(0.0, ua.VariantType.Double)


server = Server()
server.set_endpoint("opc.tcp://0.0.0.0:4840")
server.set_server_name("UM_SMART_OPCUA")

uri = "urn:um-smart-opcua"
idx = server.register_namespace(uri)

objects = server.get_objects_node()

device = objects.add_object(idx, "Device_2")

energy_var = device.add_variable(idx, "A+0", 0.0)
energy_var.set_writable(False)

api_var = ApiVariable()

def read_callback(node, attr):
    if attr == ua.AttributeIds.Value:
        return api_var.read()

    return None


server.start()

print("OPC server started")

while True:

    value = api_var.read().Value
    energy_var.set_value(value)

    time.sleep(1)
