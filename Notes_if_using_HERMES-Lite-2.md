# Some notes if using Hermes Lite 2 as SDR-TRX with deskHPSDR #

## Get full 5W output ##

After compilation and installation you need to adjust the output power of the 5W PA from the Hermes Lite 2. Go into the **Menu > Radio Menu** and tick **"PA enable"**. Then go into the **PA Menu** and set all values to 38.8 for full 5W output, otherwise you don't reach the maximum out of the Hermes Lite 2. Set the **MAX POWER** to 5W in the PA Menu if using Hermes Lite 2.<br>
If using the N2ADR low pass filter board:<br>
You will lost about 0.5..1W TX power due to the insert loss of this filter board. You need to check in **Radio Menu** too, if **N2ADR** is selected under **Filter Board**. You need also to check the settings for the N2ADR low pass filter board in the **OC Menu**. This N2ADR low pass filter board will be controlled via internal I2C bus inside the HL2, so it is important that the correct setup for this N2ADR low pass filter board is done.<br>
If using the SquareSDR:<br>
If using the SquareSDR, the low pass filter is part of the mainboard. But the setup is the same like using a HL2 in combination with the N2ADR low pass filter board. You need select **N2ADR** under **Filter Board** in **Radio Menu** too. Also check the correct settings in the **OC Menu**.

## Audio-Setup (if not using the HL2+ extension board) ##

If not using the HL2+ extension board (which is not a part of the original HL2 and only availible as a third-party extension) you need to setup all audio interfaces to **"Local input"** and **"Local output"**. Thats means, you need to use your internal audio interfaces of your computer. Select the audio input and audio output you want to use.

## Band voltage output ##

For using the bandvoltage output option of the Hermes Lite 2, you need to tick on **Band volts / Dither bit** in the **RX Menu**.

## S-Meter calibration ##

For showing correct S-Meter values you need adjust the **ADC Gain Calibration** in the **Radio Menu**:
Use a RF generator and set the frequency to 10.0 MHz, deskHPSDR set to RX 10.0 MHz too in mode CWL. Set the level in the RF generator to -73dbm and the waveform to sine wave, which represent a S-value S9 (< 30 MHz) unmodulated carrier (like CW). Set the **Meter Type** to **digital** in the **Meter Menu**. Now increase or decrease the **ADC Gain Calibration** in the **Radio Menu** until you see -73dbm in the S-Meter display. Now our S-Meter is calibrated correct.<br>
Notice: The pre-defined value +14db after first setup a HL2 is more or less only a average value and can differ for each HL2, so I recommend the  precise calibration as just described.

## Using MIDI for control deskHPSDR ##

deskHPSDR has a build-in MIDI support. You can use MIDI controller for the most functions (like a rotary switch as VFO knob), the assignments needs to be done in **MIDI Menu**. You can also use DIY controller (e.g. ESP32 or Arduino based) for developing you own MIDI controller with the help of their MIDI libraries.

## Using GPIO (Raspberry Pi only) ##

If using GPIO control lines (Raspberry Pi only), you need to set **GPIO=ON** in the ```make.config.deskhpsdr``` before you start compiling deskHPSDR. With other OS like macOS we cannot use GPIO, because such computers like Macs havn't any GPIO. Use a DIY MIDI device/controller instead and control deskHPSDR via MIDI. Functions are accessible via GPIO can be used via MIDI too.

## Change IP parameter of the HL2 (change MAC address and/or set a fixed IP address) ##

Per default the HL2 is configured as a DHCP client. That means, the HL2 will get his IP address from a running DHCP server inside your network. But it's possible to put a fixed IP address into the HL2. **deskHPSDR itself cannot adjust the IP address of the HL2**.<br>
But there are exist a Python-Extension ```hermeslite.py``` for doing this - outside deskHPSDR.<br>
Look here [https://github.com/softerhardware/Hermes-Lite2/tree/master/software/hermeslite](https://github.com/softerhardware/Hermes-Lite2/tree/master/software/hermeslite). At the end the fixed IP address need to be written in the EEPROM of the HL2. The same procedure is needed if you want to change the MAC address of the HL2. **ALL HL2 have the same(!) MAC address per factory-default**. Only if you want to use more than one HL2 at the same time inside your network, you need to change it's MAC address. That can be done with the Python-Extension ```hermeslite.py``` too and will be written to the EEPROM of the selected HL2. If you have a WINDOWS PC, you can use the app SDR Console, which supports both variants (fixed IP and change MAC) for user-defined adjustment of the network parameters into the HL2-EEPROM. **I recommend strongly the last - using SDR Console**. That's a lot easier as the same to do with the Python-Extension ```hermeslite.py```. After all you can try to connect your HL2 again with deskHPSDR.