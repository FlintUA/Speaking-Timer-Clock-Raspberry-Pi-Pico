from machine import Pin
import micropython


class Rotary:
    """Simple quadrature rotary encoder driver for MicroPython.

    The switch pin is optional. This is important when the encoder push button
    is handled by another library (for example picozero.Button), because only
    one IRQ handler should own a GPIO pin.
    """

    ROT_CW = 1
    ROT_CCW = 2
    SW_PRESS = 4
    SW_RELEASE = 8

    def __init__(self, dt, clk, sw=None):
        self.dt_pin = Pin(dt, Pin.IN, Pin.PULL_UP)
        self.clk_pin = Pin(clk, Pin.IN, Pin.PULL_UP)
        self.sw_pin = None
        self.handlers = []

        self.last_status = (self.dt_pin.value() << 1) | self.clk_pin.value()
        self.dt_pin.irq(
            handler=self.rotary_change,
            trigger=Pin.IRQ_FALLING | Pin.IRQ_RISING,
        )
        self.clk_pin.irq(
            handler=self.rotary_change,
            trigger=Pin.IRQ_FALLING | Pin.IRQ_RISING,
        )

        if sw is not None:
            self.sw_pin = Pin(sw, Pin.IN, Pin.PULL_UP)
            self.last_button_status = self.sw_pin.value()
            self.sw_pin.irq(
                handler=self.switch_detect,
                trigger=Pin.IRQ_FALLING | Pin.IRQ_RISING,
            )

    def rotary_change(self, pin):
        new_status = (self.dt_pin.value() << 1) | self.clk_pin.value()
        if new_status == self.last_status:
            return

        transition = (self.last_status << 2) | new_status
        if transition == 0b1110:
            micropython.schedule(self.call_handlers, Rotary.ROT_CW)
        elif transition == 0b1101:
            micropython.schedule(self.call_handlers, Rotary.ROT_CCW)

        self.last_status = new_status

    def switch_detect(self, pin):
        if self.sw_pin is None:
            return

        current = self.sw_pin.value()
        if self.last_button_status == current:
            return

        self.last_button_status = current
        if current:
            micropython.schedule(self.call_handlers, Rotary.SW_RELEASE)
        else:
            micropython.schedule(self.call_handlers, Rotary.SW_PRESS)

    def add_handler(self, handler):
        self.handlers.append(handler)

    def call_handlers(self, event_type):
        for handler in self.handlers:
            handler(event_type)
