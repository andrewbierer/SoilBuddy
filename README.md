<b>Repository for implementing the AP access point programing, as prototyped in the SoilBuddy device, on the Open_Irr system.<br> 
This project falls under the MIT license. </b><br>

<b>---Quick Start---</b><br>
  1) Power on the Open_Irr unit, the Raspberry Pi Pico W module will launch a WIFI access point automatically.
  2) Connect to the generated access point "Open_Irr_AP".
  3) From the landing page select "Edit Settings"; on the next page modify the settings as desired for your intended use of the Open_Irr system.
  4) Select "Apply Settings" to save and apply the new settings.

<b>---Landing Page Options---</b><br>
> Note the ability to configure the WiFi connection has no practical application at this time in development. 
  1) Edit Settings
  2) View Files
  3) Data Dashboard
  4) Toggle All Valves
  5) Reset Wifi
 

  <b>---Open_Irr Settings---</b><br>
    1) Select Timezone (set the real time clock module).<br>
    2) 1 <br>
    3) 2 <br>
    4) 3 <br>

  <b>---View Files---</b><br>
    View and apply settings saved on the Sd card.<br>

  <b>---Data Dashboard---</b><br>
    Pulls from data and error files on the Sd card to provide a simplistic display from recent measures.<br>

  <b>---Toggle All Valves---</b><br>
    There are two buttons for selection of "ON" and "OFF". When the selection is made <b>the corresponding change will be applied and remain applied until settings are changed appropriately.</b><br>

    "ON" Will change the state of all valves "ON" (Circuit Closed, Physical Valve Open, Water Flowing).<br>
    "OFF" Will change the state of all valves "OFF" (Circuit Open, Physical Valve Closed, Water NOT Flowing).<br>

  <b>---Reset Wifi---</b><br>
    If a WiFi connection was made, this will erase the memory of the WiFi credentials. 
