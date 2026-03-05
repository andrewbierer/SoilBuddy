__version__ = "0.0.2"

# highly recommended to set a lowish garbage collection threshold
# to minimise memory fragmentation as we sometimes want to
# allocate relatively large blocks of ram.
import gc, os, machine # type: ignore
gc.threshold(50000)

# phew! the Pico (or Python) HTTP Endpoint Wrangler
from . import logging

# determine if remotely mounted or not, changes some behaviours like
# logging truncation
remote_mount = False
try:
  os.statvfs(".") # causes exception if remotely mounted (mpremote/pyboard.py)
except:
  remote_mount = True

def get_ip_address():
  import network # type: ignore
  try:
    return network.WLAN(network.STA_IF).ifconfig()[0]
  except:
    return None
  
# def disconnect_from_wifi():
#     """Completely disconnects from WiFi and deactivates station mode"""
#     import network # type: ignore
#     import time
    
#     wlan = network.WLAN(network.STA_IF)
    
#     # 1. First try normal disconnect
#     try:
#         if wlan.isconnected():
#             wlan.disconnect()
#             time.sleep(1)  # Allow graceful disconnect
            
#         # 2. Force deactivate if still connected
#         if wlan.active():
#             wlan.active(False)
            
#         # 3. Verify disconnection
#         start_time = time.time()
#         while wlan.isconnected() and (time.time() - start_time < 5):  # 5s timeout
#             time.sleep(0.5)
            
#         # 4. Final cleanup
#         if wlan.active():  # Double-check
#             wlan.active(False)
            
#         return True
#     except Exception as e:
#         print(f"Disconnect error: {e}")
#         return False

def is_connected_to_wifi():
  import network, time # type: ignore
  wlan = network.WLAN(network.STA_IF)
  return wlan.isconnected()

# helper method to quickly get connected to wifi
def connect_to_wifi(ssid, password, timeout_seconds=30):
  import network, time # type: ignore

  statuses = {
    network.STAT_IDLE: "idle",
    network.STAT_CONNECTING: "connecting",
    network.STAT_WRONG_PASSWORD: "wrong password",
    network.STAT_NO_AP_FOUND: "access point not found",
    network.STAT_CONNECT_FAIL: "connection failed",
    network.STAT_GOT_IP: "got ip address"
  }

  wlan = network.WLAN(network.STA_IF)
  wlan.active(True)    
  wlan.connect(ssid, password)
  start = time.ticks_ms()
  status = wlan.status()

  logging.debug(f"  - {statuses[status]}")
  while not wlan.isconnected() and (time.ticks_ms() - start) < (timeout_seconds * 1000):
    new_status = wlan.status()
    if status != new_status:
      logging.debug(f"  - {statuses[status]}")
      status = new_status
    time.sleep(0.25)

  if wlan.status() == network.STAT_GOT_IP:
    return wlan.ifconfig()[0]
  return None


# helper method to put the pico into access point mode
def access_point(ssid, password = None):
  import network # type: ignore

  # start up network in access point mode  
  wlan = network.WLAN(network.AP_IF)
  wlan.config(essid=ssid)
  if password:
    wlan.config(password=password)
  else:    
    wlan.config(security=0) # disable password
  wlan.active(True)

  return wlan
