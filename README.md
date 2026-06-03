TEMP<br>
<br>
![ECHOXL Logo](/images/ECHOXL_Logo.png) <br>
The EchoXL is a custom designed 7 segment clock, using four 4" displays.<br>
I purchased these displays from [Sc Electronics](http://sconline.com.tw/) in Guanghua Electronic Plaza, Taiwan. <br>
They are labeled "LS4006SRWK" and the datasheet can be found [here](https://en.lenoo.com/product-detail-749164.html).<br>
I selected the ESP32-C3 (WROOM-02-N4) MCU for its onboard Wi-Fi, USB programming, appropriate pincount and affordability.<br>
The board uses a CH221K to negotiate 12V via USB-C PD.<br>
The 12V USB power is fed into a pair of TLC5926 16 channel current control LED driver ICs.<br>
A simple buck supply (AP63203) produces the 3.3V rail for the ESP32-C3 and the simple peripherals.<br>
These peripherals being an LDR for ambient light brightness adjustment and a single user button.<br>
<br>
The provided firmware is a polished, simple digital clock.<br>
The clock will attempt to connect to a saved network on boot. If none is found, it will set the time to 12:00 and blink a status segment.<br>
The user can then connect to an HTML captive portal GUI produced by the clock. <br>
The user may enter the current time manually, or input a network SSID and Password. <br>
The clock will attempt to connect to said network and use NTP to set the current time.<br>
The clock saves network settings through reboot and will attempt to reconnect on boot. Failure to connect will restart the loop.<br>
<br>
Additionally, the user may short-press the user button to cycle through four brightness modes:<br>
Automatic, Low, Medium, High<br>
The user may long-press the user button to switch between 12h and 24h time.<br>
Holding the user button on startup will reset the network settings. <br>
<br>
Known issues:
