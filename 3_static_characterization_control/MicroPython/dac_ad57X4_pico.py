from struct import pack
from time import sleep
from machine import Pin, SPI, SoftI2C
import sys
import machine


class dac_ad57X4:
    out_bit_mode: int
    out_bit: int
    out_addon: str
    __offset: int

    """Class for handling the DAC AD57X4"""

    def __init__(self, dev_type: int, spi_cs_sel, spi_handler):
        """Init of the Pi SPI module
        Link to Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/ad5724_5734_5754.pdf
        Args:
            dev_type:   Mode of selected device (0= AD5724, 1= AD5734, 2= AD5754)
        Returns:
            None
        """
        self.init_done = False

        self.__gpio_nldac = None
        self.__gpio_nclr = None
        self.__gpio_ncs = spi_cs_sel

        self.__sel_bit_mode(dev_type)
        self.use_twos_complement = True
        self.out_unipolar = False
        self.out_adr = [0, 1, 2, 3, 4]

        self.__spi_handler = spi_handler
        #self.__init_spi(int(spi_speed), spi_clk, spi_mosi, spi_cs_sel)

    def __init_spi(self, spi_speed: int, spi_clk: int, spi_mosi: int, spi_ncs: int) -> None:
        self.__gpio_ncs = Pin(spi_ncs, Pin.OUT, value=1)
        self.__spi_handler = SPI(0,  # SPI (GP18=SCK, GP19=MOSI, GP16=MISO)
                                 baudrate=spi_speed,
                                 polarity=0,
                                 phase=0,
                                 bits=8,
                                 firstbit=machine.SPI.MSB,
                                 sck=machine.Pin(spi_clk),
                                 mosi=machine.Pin(spi_mosi),
                                 miso=machine.Pin(16))

    def __twos(self, data_in: int, num_bytes=2):
        b = data_in.to_bytes(num_bytes, byteorder=sys.byteorder, signed=False)
        return int.from_bytes(b, byteorder=sys.byteorder, signed=True)

    def __sel_bit_mode(self, dev_type: int) -> None:
        if dev_type == 0:
            self.out_bit_mode = 0
            self.out_bit = 12
            self.out_addon = '0000'
        elif dev_type == 1:
            self.out_bit_mode = 1
            self.out_bit = 14
            self.out_addon = '00'
        elif dev_type == 2:
            self.out_bit_mode = 2
            self.out_bit = 16
            self.out_addon = ''
        else:
            self.out_bit_mode = 0
            self.out_bit = 12
            self.out_addon = '0000'
        self.__offset = int(2 ** (self.out_bit - 1))

    def activate_using_hw_nldac(self, gpio_bcm_nldac) -> None:
        """"""
        self.__gpio_nldac = gpio_bcm_nldac
        self.__gpio_nldac.low()

    def activate_using_hw_nclr(self, gpio_bcm_nclr) -> None:
        """"""
        self.__gpio_nclr = gpio_bcm_nclr
        self.__gpio_nclr.low()

    def reset_dac(self, num_rpt=2, period=0.1):
        """Reset of the output values"""
        # --- Reset of device
        for idx in range(num_rpt):
            # --- Pin Reset
            if self.__gpio_nclr:
                self.__gpio_nclr.value(0)
                sleep(period)
                self.__gpio_nclr.value(1)
                sleep(period)

    def init(self, en_ch: int, use_unipolar: bool, do_rst=True, use_twos_complement=True) -> None:
        """"""
        self.out_unipolar = use_unipolar
        self.use_twos_complement = use_twos_complement
        send_transmit = list()
        if do_rst:
            self.reset_dac()

        # Writing to the "Power control register"
        power_data = 0
        if en_ch == 4:  # Assuming 4 means "all channels"
            power_data = 15  # 0b1111, enables all 4 channels
        elif 0 <= en_ch <= 3:
            power_data = 1 << en_ch  # Enable a single channel
        else:
            power_data = 0  # Or raise an error

        send_data = pack('<B', int('00010000', 2)) + pack('<B', 0) + pack('<B', power_data)
        send_transmit.append(send_data)
        sleep(0.1)

        # Writing to the "Control register"
        send_data = pack('<B', int('00011001', 2)) + pack('<B', 0) + pack('<B', int('00000000', 2))
        send_transmit.append(send_data)

        # Writing to the "Output range select register"
        val = 0 if use_unipolar else 3
        send_data = pack('<B', int('00001100', 2)) + pack('<B', 0) + pack('<B', val)
        send_transmit.append(send_data)

        self.write_bytes(send_transmit)

    def prepare_data(self, data_in: int, sel_ch: int) -> bytes:
        """"""
        if self.use_twos_complement:
            val0 = Bits(int=data_in, length=self.out_bit).bin + self.out_addon
        else:
            val0 = f'{int(data_in):0{self.out_bit}b}' + self.out_addon

        val0 = int(val0, 2)
        val0 = val0.to_bytes(2, 'big')
        data_out = pack('<B', self.out_adr[sel_ch]) + val0
        return data_out

    def write_bytes(self, bytes2send: list) -> None:
        """"""
        
        for data in bytes2send:
            self.__gpio_ncs.low()
            self.__spi_handler.write(data)
            self.__gpio_ncs.high()
            sleep(0.001)

    def load_dac(self) -> None:
        send_data = pack('<B', int('00011101', 2)) + pack('<B', 0) + pack('<B', int('00000000', 2))
        self.write_bytes([send_data])

    def output_simultan_set(self) -> None:
        """"""
        if self.__gpio_nldac:
            self.__gpio_nldac.value(0)

    def output_simultan_deactivate(self) -> None:
        """"""
        if self.__gpio_nldac:
            self.__gpio_nldac.value(1)

    def set_output(self, data_in: int, sel_ch: int) -> None:
        """"""
        data = self.prepare_data(data_in, sel_ch)
        #print(data)
        self.write_bytes([data])
        self.load_dac()

    def stop_device(self) -> None:
        """Stopping the DAC functionalities with resetting"""
        self.reset_dac()
        sleep(0.1)

        if self.__gpio_nclr:
            self.__gpio_nclr.value(0)
            sleep(0.1)

        if self.__gpio_nldac:
            self.__gpio_nldac.value(0)
            sleep(0.1)

        self.__spi_handler.deinit()
        sleep(0.1)

