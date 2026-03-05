
# USDA 
# Torrence Washington
# July 2025

# Modified by Andrew Bierer 2026

from phew import server, logging, access_point, dns, connect_to_wifi, is_connected_to_wifi
from phew.template import render_template
import json, sdcard, os, _thread, machine, utime, gc, sys, network, socket # type: ignore
from machine import SPI, Pin # type: ignore
gc.threshold(50000) # setup garbage collection

APP_TEMPLATE_PATH = "app_templates"
AP_NAME = "USAP"
WIFI_FILE = "wifi.json"
SETTINGS_FILE = "settings.json"
READING_FILE = "reading.json"
SD_MOUNT_PATH = '/sd'
global_ip_address = None
SD_SAVES = 1
SPI_BUS = 0
SCK_PIN = 2
MOSI_PIN = 3
MISO_PIN = 4
CS_PIN = 5
onboard_led = machine.Pin("LED", machine.Pin.OUT)

# resets pico, working getting switch to work (pontentially delete or ignore)
def machine_reset():
    utime.sleep(5) # waits a second before going forward 
    print("Resetting...")
    machine.reset() # turns off pi

# starting page 
def app_index(request):
    return render_template(f"{APP_TEMPLATE_PATH}/index.html")

# configure the wifi connection
def app_configure(request):
    # Save WiFi credentials first
    with open(WIFI_FILE, "w") as f:
        json.dump(request.form, f)
    
    ssid = request.form.get("ssid", "")
    wlan = network.WLAN(network.STA_IF)
    
    # Check if already connected
    if wlan.isconnected():
        ip = wlan.ifconfig()[0]  # Get current IP
        return server.Response(f"""
        <!DOCTYPE html>
        <html>
            <head>
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <title>Wifi Configured</title>
            </head>
            <body>
                <h1>Wifi Configured</h1>
                <p>The Raspberry Pi Pico is connected to "{ssid}" at {ip}</p>
                <button onclick="window.location.href='/'">Continue</button>
            </body>
        </html>
        """)
    
    # Start connection if not already connected
    if not wlan.active() or not wlan.isconnected():
        def _connect_to_wifi():
            try:
                connect_to_wifi(request.form["ssid"], request.form["password"])
            except Exception as e:
                logging.error(f"Connection failed: {str(e)}")
        
        _thread.start_new_thread(_connect_to_wifi, ())
    
    # Show connection status page with auto-refresh
    current_ip = wlan.ifconfig()[0] if wlan.isconnected() else "Not assigned yet"
    return server.Response(f"""
    <!DOCTYPE html>
    <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Wifi Configured</title>
            <meta http-equiv="refresh" content="3">
        </head>
        <body>
            <h1>Wifi Configured</h1>
            <p>The Raspberry Pi Pico is connecting to "{ssid}"...</p>
            <p>Go to IP Address: {current_ip}</p>
            {'<button onclick="window.location.href=\'/\'">Continue</button>' if wlan.isconnected() else ''}
        </body>
    </html>
    """)

# LED toggle, can ignore/delete
def app_toggle_led(request):
        onboard_led.toggle()
        return "OK"

# reset wifi config 
def app_reset(request):
    """Immediately serves the reset page, then triggers async reset"""
    # Start reset sequence after small delay (allows page to load)
    _thread.start_new_thread(_delayed_reset, ())
    
    return render_template(
        f"{APP_TEMPLATE_PATH}/reset.html",
        access_point_ssid=AP_NAME,
        ip="192.168.4.1",  # Default AP IP
        reconnect_delay=3   # Seconds before auto-reconnect attempt
    )

# delay reset to show reset.html
def _delayed_reset():
    """Threaded reset with proper timing"""
    utime.sleep(1.5)  # Critical: Allow page to fully load first
    _perform_network_reset()
    sys.exit()

# disconnect from wifi & reset
def _perform_network_reset():
    """Atomic reset operations"""
    try:
        # 1. Delete credentials
        if WIFI_FILE in os.listdir():
            os.remove(WIFI_FILE)
        
        # 2. Controlled disconnect
        wlan = network.WLAN(network.STA_IF)
        if wlan.isconnected():
            wlan.disconnect()
            utime.sleep(1)  # Allow graceful disconnect
            
        # 3. Ensure interface down
        wlan.active(False)
        utime.sleep(0.5)
        
        # 4. Restart AP
        global ap
        ap = access_point(AP_NAME)
        logging.info("AP restarted successfully")
        
    except Exception as e:
        logging.error(f"Reset failed: {str(e)}")

# options page 
def app_change_options(request):
    return render_template(f"{APP_TEMPLATE_PATH}/options.html")

# shows changes were saved to a file
def app_save_changes(request):
    # Save changes to  file
    with open(SETTINGS_FILE, "w") as f:
        json.dump(request.form, f)
    
    # Attempt to transfer to SD card
    transfer_result = transfer_file_to_sd() if SD_MOUNTED else "SD card not available"
    
    # Get current SD card contents
    sd_files=list_sd_files()
    
    return render_template(f"{APP_TEMPLATE_PATH}/save_changes.html",
                         transfer_result=transfer_result,
                         sd_files=sd_files)

# view saves on sd card
def view_saves(request):
    try:
        files = [f for f in os.listdir(SD_MOUNT_PATH) if f != READING_FILE]  # Filter out reading.json
        file_links = ""
        for filename in files:
            file_links += f"""
            <div style="margin: 10px 0; padding: 10px; border: 1px solid #ccc; display: flex; justify-content: space-between; align-items: center;">
                <a href="/download/{filename}">{filename}</a>
                <div>
                    <button onclick="window.location.href='/apply?file={filename}'" style="margin-right: 5px;">Apply</button>
                    <button onclick="window.location.href='/rename-file?file={filename}'" style="margin-right: 5px;">Rename</button>
                    <button onclick="if(confirm('Delete {filename}?')) window.location.href='/delete-file?file={filename}'" style="background-color: #ff4444; color: white;">Delete</button>
                </div>
            </div>
            """
        if not files:
            file_links = "<p>No files found.</p>"
    except Exception as e:
        file_links = f"<p>Error: {str(e)}</p>"
    
    return server.Response(f"""
    <!DOCTYPE html>
    <html>
    <head>
        <title>SD Card Files</title>
        <style>
            body {{ font-family: Arial; margin: 20px; }}
            button {{ margin-left: 10px; }}
        </style>
    </head>
    <body>
        <h1>Files on SD Card</h1>
        {file_links}
        <br>
        <button onclick="window.location.href='/upload'">Upload File</button>
        <button onclick="window.location.href='/'">Go Home</button>
    </body>
    </html>
    """)

# temperature reader on pico, can ignore/delete
def app_get_temperature(request):
    # Not particularly reliable but uses built in hardware.
    # Algorithm used here is from:
    # https://www.coderdojotc.org/micropython/advanced-labs/03-internal-temperature/
    sensor_temp = machine.ADC(4)
    reading = sensor_temp.read_u16() * (3.3 / (65535))
    temperature = 27 - (reading - 0.706)/0.001721
    return f"{round(temperature, 1)}"

# Add this function to display SD card contents
def list_sd_files():
    try:
        files = os.listdir(SD_MOUNT_PATH)
        return "\n".join(files) if files else "No files found on SD card"
    except OSError:
        return "SD card not accessible"

# Modified transfer_file_to_sd() function
def transfer_file_to_sd():
    # 1. Check settings file
    try:
        with open(SETTINGS_FILE, 'r') as f:
            content = f.read()
            if not content.strip():
                return "No file to transfer (empty settings file)!\n" + list_sd_files()
            
            try:
                json.loads(content)  # Validate JSON
            except ValueError:
                return "Invalid JSON in settings file!\n" + list_sd_files()
    except OSError:
        return "No settings file to transfer!\n" + list_sd_files()

    # 2. Verify SD card
    if not SD_MOUNTED:
        return "SD card not mounted!\n" + list_sd_files()

    # 3. Find next available filename
    save_number = 1
    while True:
        new_filename = f"{SD_MOUNT_PATH}/save_settings{save_number}.json"
        try:
            with open(new_filename, 'r'):
                save_number += 1
        except OSError:
            break

    # 4. Write to SD card
    try:
        with open(new_filename, 'w') as f:
            f.write(content)
            f.close()
            
        # Return success message with updated file list
        return (f"Transfer successful! Saved to {new_filename}\n\n"
                f"Current SD card contents:\n{list_sd_files()}")
    except OSError as e:
        return f"Failed to write to SD card: {e}\n\nCurrent contents:\n{list_sd_files()}"


    except Exception as e:
        return f"An error occurred: {str(e)}", 500
    
# apply saved settings to reading.json
def apply_settings(request):
    filename = request.query.get("file")
    if not filename:
        return "No file specified", 400
    
    try:
        # Security check to prevent directory traversal
        if filename.startswith("/") or filename.startswith(".."):
            return "Invalid filename", 400
            
        source_path = f"{SD_MOUNT_PATH}/{filename}"
        dest_path = f"{SD_MOUNT_PATH}/{READING_FILE}"  # Use global variable
        
        # Read the source file
        with open(source_path, "r") as src_file:
            content = src_file.read()
        
        # Write to reading.json
        with open(dest_path, "w") as dest_file:
            dest_file.write(content)
            
        return server.Response(f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Settings Applied</title>
        </head>
        <body>
            <h1>Settings Applied Successfully</h1>
            <p>Content from {filename} has been written to {READING_FILE}</p>
            <br>
            <button onclick="window.location.href='/view'">Back to Files</button>
            <button onclick="window.location.href='/'">Go Home</button>
        </body>
        </html>
        """)
        
    except Exception as e:
        return f"Error applying settings: {str(e)}", 500

# rename saved settings files 
def rename_file(request):
    # grabs names from post request 
    if request.method == "POST":
        old_name = request.form.get("old_name")
        new_name = request.form.get("new_name")
        
        try:
            if old_name and new_name:
                # Ensure we're only working with files in the SD card directory
                if not old_name.startswith("/") and not old_name.startswith(".."):
                    old_path = f"{SD_MOUNT_PATH}/{old_name}"
                    new_path = f"{SD_MOUNT_PATH}/{new_name}"
                    
                    # Check if file exists and new name is valid
                    if old_name in os.listdir(SD_MOUNT_PATH) and new_name.strip():
                        os.rename(old_path, new_path) # rename file with new name
                        return render_template(f"{APP_TEMPLATE_PATH}/rename_success.html", 
                                            old_name=old_name, 
                                            new_name=new_name)
        
        except Exception as e:
            logging.error(f"Error renaming file: {e}")
    
    # If GET request or error occurred, show the rename form
    files = []
    try:
        files = os.listdir(SD_MOUNT_PATH)
    except Exception as e:
        logging.error(f"Error listing SD card files: {e}")
    
    return render_template(f"{APP_TEMPLATE_PATH}/rename_file.html", files=files)

# delete files from sd 
def delete_file(request):
    if request.method == "POST":
        filename = request.form.get("filename")
        
        try:
            if filename:
                # Security check to prevent directory traversal
                if not filename.startswith("/") and not filename.startswith(".."):
                    file_path = f"{SD_MOUNT_PATH}/{filename}"
                    
                    # Check if file exists
                    if filename in os.listdir(SD_MOUNT_PATH):
                        os.remove(file_path)
                        # Return success response
                        return server.Response(f"""
                        <!DOCTYPE html>
                        <html>
                        <head>
                            <meta name="viewport" content="width=device-width, initial-scale=1.0">
                            <title>File Deleted</title>
                        </head>
                        <body>
                            <h1>File Deleted Successfully</h1>
                            <p>Deleted file: {filename}</p>
                            <br>
                            <button onclick="window.location.href='/delete-file'">Delete Another</button>
                            <button onclick="window.location.href='/view'">View Files</button>
                            <button onclick="window.location.href='/'">Go Home</button>
                        </body>
                        </html>
                        """)
        
        except Exception as e:
            logging.error(f"Error deleting file: {e}")
            error_html = f"""
            <!DOCTYPE html>
            <html>
            <body>
                <h1>Error</h1>
                <p>Failed to delete file: {str(e)}</p>
                <button onclick="window.location.href='/delete-file'">Try Again</button>
            </body>
            </html>
            """
            return server.Response(error_html, status=500)
    
    # GET request - show delete form
    try:
        files = os.listdir(SD_MOUNT_PATH)
        
        file_items = ""
        for filename in files:
            file_items += f"""
            <div style="margin:10px; padding:10px; border:1px solid #ccc; display:flex; justify-content:space-between;">
                <span>{filename}</span>
                <button onclick="if(confirm('Delete {filename}?')) {{ 
                    const form = document.createElement('form');
                    form.method = 'POST';
                    form.action = '/delete-file';
                    const input = document.createElement('input');
                    input.type = 'hidden';
                    input.name = 'filename';
                    input.value = '{filename}';
                    form.appendChild(input);
                    document.body.appendChild(form);
                    form.submit();
                }}" style="background-color:#ff4444; color:white;">Delete</button>
            </div>
            """
        
        html_content = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Delete Files</title>
            <style>
                body {{ font-family: Arial; margin: 20px; }}
                button {{ padding: 5px 10px; margin-left: 5px; }}
            </style>
        </head>
        <body>
            <h1>Delete Files</h1>
            {file_items if files else "<p>No files found</p>"}
            <br>
            <button onclick="window.location.href='/'">Go Home</button>
            <button onclick="window.location.href='/view'">View Files</button>
        </body>
        </html>
        """
        
        return server.Response(html_content)
        
    except Exception as e:
        logging.error(f"Error listing files: {e}")
        return server.Response(f"""
        <!DOCTYPE html>
        <html>
        <body>
            <h1>Error</h1>
            <p>Could not list files: {str(e)}</p>
            <button onclick="window.location.href='/'">Go Home</button>
        </body>
        </html>
        """, status=500)

# downloads file from /view
@server.route("/download/<filename>")
def download_file(request, filename):
    try:
        with open(f"{SD_MOUNT_PATH}/{filename}", "rb") as f:
            content = f.read() # gets content from file
        return server.Response( # downloads file to device
            content,
            headers={"Content-Disposition": f"attachment; filename={filename}"} 
        )
    except Exception as e:
        return f"Error downloading file: {str(e)}", 404

@server.route("/configured-refresh")
def configured_refresh(request):
    # Reuse the same template but with current status
    with open(WIFI_FILE) as f:
        wifi_credentials = json.load(f)
    
    return render_template(
        f"{APP_TEMPLATE_PATH}/configured.html", 
        ssid=wifi_credentials["ssid"],
        ip=global_ip_address or "(connecting...)",
        show_continue=(global_ip_address is not None and "failed" not in str(global_ip_address))
    )

def app_catch_all(request):
        return "Not found.", 404

# Update your SD card initialization to set SD_MOUNTED
try:
    spi = SPI(SPI_BUS, sck=Pin(SCK_PIN), mosi=Pin(MOSI_PIN), miso=Pin(MISO_PIN))
    cs = Pin(CS_PIN)
    sd = sdcard.SDCard(spi, cs)
    os.mount(sd, SD_MOUNT_PATH)
    print("SD card mounted successfully")
    print("Initial SD card contents:", list_sd_files())
    SD_MOUNTED = True



except Exception as e:
    print('SD card initialization failed:', e)
    SD_MOUNTED = False



# Routes to different pages
server.add_route("/", handler = app_index, methods = ["POST", "GET"])
server.add_route("/configure", handler = app_configure, methods= ["POST", "GET"])
server.add_route("/reset", handler = app_reset, methods = ["GET"])
server.add_route("/toggle", handler = app_toggle_led, methods = ["GET"])
server.add_route("/view", handler = view_saves, methods = ["GET"])
server.add_route("/temperature", handler = app_get_temperature, methods = ["GET"])
server.add_route("/options", handler = app_change_options, methods= ["POST", "GET"])
server.add_route("/savechanges", handler = app_save_changes, methods= ["POST", "GET"])
server.add_route("/rename-file", handler=rename_file, methods=["GET", "POST"])
server.add_route("/delete-file", handler=delete_file, methods=["GET", "POST"])
server.add_route("/apply", handler=apply_settings, methods=["GET"])
server.set_callback(app_catch_all)

# Set to Accesspoint mode
ap = access_point("USAP")  # Change this to whatever Wi-Fi SSID you wish
ip = ap.ifconfig()[0]                   # Grab the IP address and store it
logging.info(f"starting DNS server on {ip}")
dns.run_catchall(ip)                    # Catch all requests and reroute them
server.run()                            # Run the server
logging.info("Webserver Started")
