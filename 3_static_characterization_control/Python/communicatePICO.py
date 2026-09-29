from time import sleep
import numpy as np
import serial
from serial.tools import list_ports
import pyvisa
rm = pyvisa.ResourceManager()
import matplotlib.pyplot as plt

#Find correct GPIB0 address
print("Possible pyvisa devices:")
print(rm.list_resources())

pico_inst = rm.open_resource('COM5')
#print(pico_inst.query('*IDN?'))
sleep(1)
response = pico_inst.read()  # Wait for the "Done" or "ERROR"
print(f"Pico responded: {response.strip()}")

GPIB_address = 'GPIB0::1::INSTR'
measurement_inst = rm.open_resource('GPIB0::1::INSTR')
print("Connecting to address", GPIB_address, "and device:")
print(measurement_inst.query("*IDN?"))
measurement_inst.write('*RST')  # Reset to a known state
measurement_inst.write(':SENS:FUNC "CURR:DC"')
measurement_inst.write(':SENS:CURR:DC:RANG:AUTO ON')
measurement_inst.write(':SENS:CURR:DC:NPLC 1')
measurement_inst.write('FORM:ELEM READ')


# --- Measuring routine
lastStep = 0
lastFileName = "Results/StimPlat_16Bit_Full2_DAC_B_NoLoad"

#meas_dig = np.linspace(lastStep, pow(2, 16) - 1, pow(2, 16)-lastStep)
#above meas_dig is to measure full range from -200 to 200 µA
#below meas_dig is intended to partition the DAC and measure only from -20 to 20 µA
full_scale = 200
target_scale = 200
total_N = 2**16 - 1
mid_N = total_N//2
half_range = int((target_scale / full_scale) * (total_N/2))
start_at = mid_N - half_range
stop_at = mid_N + half_range

meas_dig = np.arange(start_at, stop_at + 1, dtype=int) #, 2**13-1 #insert this after stop_at +1 to do a quick run with larger steps size

meas_cur = list()

for idx, dig_val in enumerate(meas_dig):
    dig_val = int(dig_val)
    #cur_ideal = 400 * dig_val / (meas_dig.size - 1) - 200
    #below cur_ideal is for new range from -20 to 20µA
    cur_ideal = (2*target_scale) * (dig_val - start_at) / (stop_at - start_at) -target_scale
    print(f"Set dig value: {dig_val} --> Set current (ideal): {cur_ideal:3f} µA")

    STIM_Command_PICO = "STIM " + str(dig_val) + "\n"
    print(STIM_Command_PICO)
    #try writing and if error save data
    try:
        pico_inst.write(STIM_Command_PICO)
    except Exception as e:
        meas_idac = np.array(meas_cur)
        meas_dig = np.array(meas_dig)
        meas_dig.astype(int)

        fileName = lastFileName
        np.save(fileName, meas_idac)
        np.save(fileName + "_digVal", meas_dig)

        print("ERROR:", e)
        break
    if idx == 0:
        sleep(1)
    sleep(0.01)
    try:
        response = pico_inst.read()  # Wait for the "Done" or "ERROR"
        print(f"Pico responded: {response.strip()}")
    except Exception as e:
        meas_idac = np.array(meas_cur)
        meas_dig = np.array(meas_dig)
        meas_dig.astype(int)

        fileName = lastFileName
        np.save(fileName, meas_idac)
        np.save(fileName + "_digVal", meas_dig)

        print("ERROR:", e)
        break

    #sleep(0.1)

    num_samples = 10

    meas_i = list()
    for num in range(num_samples):
        try:
            i0_str = measurement_inst.query(':READ?')
        except Exception as e:
            meas_idac = np.array(meas_cur)
            meas_dig = np.array(meas_dig)
            meas_dig.astype(int)

            fileName = lastFileName
            np.save(fileName, meas_idac)
            np.save(fileName + "_digVal", meas_dig)

            print("ERROR:", e)
            break
        i0_str.strip()
        meas_i.append(float(i0_str))
        #sleep(0.01)

    meas_i = np.array(meas_i)
    print(f"Run #{idx} --> meas. current: median = {1e6 * np.median(meas_i):.3f} +/- {1e6 * np.std(meas_i):.3f} µA")
    meas_cur.append(meas_i)


#ser = serial.Serial('COM5', 115200)  # open serial port
#print(ser.name) # check which port was really used
#print(ser.readlines())
#ser.write(b'LED ON')     # write a string
#sleep(10)
#ser.write(b'LED OFF')
#ser.close()             # close port
pico_inst.write("EXIT\n")

# --- Save results
meas_idac = np.array(meas_cur)
meas_dig = np.array(meas_dig)
meas_dig.astype(int)

fileName = lastFileName
np.save(fileName, meas_idac)
np.save(fileName + "_digVal", meas_dig)

plt.boxplot(meas_idac.T*1e6)
plt.show()