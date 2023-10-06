# Mimir Head Sculpt from God of War

Wifi LED driver for the LEDs embedded in the silicone Mimir head.

This is mainly boilerplate components around an `ESP32-WROOM-32D`.
Handles powering it from USB-C & includes an USB to UART `CH340K` chip to allow reprogramming it once it is enclosed inside the head and can't be accessed directly anymore.

The PCB includes:
- A footprint for an USB-C connector
- A cable connector to bypass USB and allow plugging UART directly if anything goes wrong in the USB circuitry, or simply allow building a cheaper version without the `CH340K` for versions that we don't want to reprogram once the silicone is poured.
- A deported USB-C board to place at the back of the neck away from the main board. This 2nd board has a bunch of features in the board outline (and extra holes) to maximise the grip of the silicone around it to have less risk of ripping it out when unplugging the external USB-C cable.

## Final result
Final assembly, with the final silicone pour.

Power on:

![Turned on](Images/Final_On1.jpg) ![Turned on](Images/Final_On2.jpg)

Power off:

![Turned off](Images/Final_Off.jpg)


## Intermediate assembly
Assembly on resin skull, before silicone:

![Assembly](Images/Assembly_1.jpg)

![Assembly](Images/Assembly_2.jpg)

![Assembly](Images/Assembly_3.jpg)

## PCB assembly

![PCB Assembly](Images/PCB_Assembly_1.jpg)

![PCB Assembly](Images/PCB_Assembly_2.jpg)

## Current PCB layout
Mk1 Hardware:

![PCB](PCB/Latest_PCB.png)

![Schematic](PCB/Latest_Schematic.png)
