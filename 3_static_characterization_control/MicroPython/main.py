# MicroPython code for the Raspberry Pi Pico module
#

import time, sys
from machine import Pin, SPI, SoftI2C
from dac_ad57X4_pico import dac_ad57X4
from hw_settings_StimPlatv3 import hw_gpio
from time import sleep

led_status = Pin(9, Pin.OUT)  # Status LED on Pico
led_auto = True  # LED auto mode


gpio_pins = hw_gpio()
spi_ncs = Pin(gpio_pins.gpio_DAC_SPI_cs, Pin.OUT, value=1)
dac_nldac = Pin(gpio_pins.gpio_nldac, Pin.OUT, value=0)
dac_nrst = Pin(gpio_pins.gpio_DAC_rstn, Pin.OUT, value=0)

spi_handler = SPI(0,  # SPI (GP18=SCK, GP19=MOSI, GP16=MISO)
                                 baudrate=gpio_pins.gpio_spi_clk_freq,
                                 polarity=0,
                                 phase=1,
                                 bits=8,
                                 firstbit=SPI.MSB,
                                 sck=Pin(gpio_pins.gpio_DAC_SPI_clk),
                                 mosi=Pin(gpio_pins.gpio_DAC_SPI_MOSI),
                                 miso=Pin(16))


stimPlat = dac_ad57X4(2, spi_ncs, spi_handler)
stimPlat.activate_using_hw_nldac(dac_nldac)
stimPlat.activate_using_hw_nclr(dac_nrst)

# --- Settings    
ch_dac_sel = 4

sleep(2)

# --- Init of device
print("Init DAC")
stimPlat.init(ch_dac_sel, False, use_twos_complement=False)
en_ch0 = Pin(gpio_pins.gpio_DAC_EN_STIM_0, Pin.OUT)
en_ch1 = Pin(gpio_pins.gpio_DAC_EN_STIM_1, Pin.OUT)
en_ch2 = Pin(gpio_pins.gpio_DAC_EN_STIM_2, Pin.OUT)
en_ch3 = Pin(gpio_pins.gpio_DAC_EN_STIM_3, Pin.OUT)
en_ch0.low()
en_ch1.high()
en_ch2.low()
en_ch3.low()

led_status.high()  # flash LED twice
time.sleep_ms(250)
led_status.low()
time.sleep_ms(100)
led_status.high()
time.sleep_ms(250)
led_status.low()

version = "Pico Stim Controller - 24/11/2025"

while True:  # Main Loop - Repeat forever
    commandline = input("pico> ")
    #commandline = sys.stdin.readline()
    commandline = commandline.strip()
    parameters = commandline.split(" ")
    command = parameters[0].upper()
    commandfound = False

    if command == "HELP" or command == "?":
        commandfound = True
        print("info")
        print("led on|off|flash|auto")
        print("stim dig_ampl(int)")

    if command == "INFO" or command == "VERSION":
        commandfound = True
        print(version)

    if command == "STIM":
        try:
            commandfound = True
            if len(parameters) != 2:
                raise Exception("Stimulation amplitude (dig value) expected")
            stim_value_dig = int(parameters[1])
            stimPlat.set_output(stim_value_dig, ch_dac_sel)
            sleep(0.1)
            print("STIM ", stim_value_dig)
        except Exception as e:
            print("ERROR:", e)

    if command == "LED":
        commandfound = True
        try:
            function = parameters[1].upper()
            if function == "ON":
                led_status.high()
                print("LED switched on")
                led_auto = False
            elif function == "OFF":
                led_status.low()
                print("LED switched off")
                led_auto = False
            elif function == "FLASH":
                led_status.high()
                time.sleep_ms(150)
                led_status.low()
                time.sleep_ms(100)
                led_status.high()
                time.sleep_ms(150)
                led_status.low()
                print("LED flashed")
            elif function == "AUTO":
                led_auto = True
                print("LED set to auto")
            else:
                print("ERROR: Function not found")
        except Exception as e:
            print("ERROR:", e)

    if command == "EXIT":
        print("Exit command used")
        # --- Closing all connections
        stimPlat.stop_device()
        en_ch0.low()
        sys.exit("Exit command used")

    if command != "" and commandfound == False:
        print("ERROR: Command not found")
