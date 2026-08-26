from machine import Pin
import micropython


class Rotary:
    """Quadrature rotary encoder driver for MicroPython.

    The switch pin is optional. Rotary IRQs are coalesced into a single
    scheduled callback so a fast mechanical encoder cannot overflow
    MicroPython's small schedule queue while the main code is busy.
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
        self._pending_steps = 0
        self._rotary_scheduled = False

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
            self._switch_event = None
            self._switch_scheduled = False
            self.sw_pin.irq(
                handler=self.switch_detect,
                trigger=Pin.IRQ_FALLING | Pin.IRQ_RISING,
            )

    def rotary_change(self, pin):
        new_status = (self.dt_pin.value() << 1) | self.clk_pin.value()
        if new_status == self.last_status:
            return

        transition = (self.last_status << 2) | new_status
        self.last_status = new_status

        if transition == 0b1110:
            self._pending_steps += 1
        elif transition == 0b1101:
            self._pending_steps -= 1
        else:
            return

        if not self._rotary_scheduled:
            self._rotary_scheduled = True
            try:
                micropython.schedule(self._drain_rotary, 0)
            except RuntimeError:
                # The global MicroPython schedule queue may temporarily be full.
                # Keep the accumulated step count and allow a later edge to retry.
                self._rotary_scheduled = False

    def _drain_rotary(self, _):
        steps = self._pending_steps
        self._pending_steps = 0
        self._rotary_scheduled = False

        if steps > 0:
            for _ in range(steps):
                self.call_handlers(Rotary.ROT_CW)
        elif steps < 0:
            for _ in range(-steps):
                self.call_handlers(Rotary.ROT_CCW)

        # Edges may have arrived while handlers were running.
        if self._pending_steps and not self._rotary_scheduled:
            self._rotary_scheduled = True
            try:
                micropython.schedule(self._drain_rotary, 0)
            except RuntimeError:
                self._rotary_scheduled = False

    def switch_detect(self, pin):
        if self.sw_pin is None:
            return

        current = self.sw_pin.value()
        if self.last_button_status == current:
            return

        self.last_button_status = current
        self._switch_event = Rotary.SW_RELEASE if current else Rotary.SW_PRESS

        if not self._switch_scheduled:
            self._switch_scheduled = True
            try:
                micropython.schedule(self._drain_switch, 0)
            except RuntimeError:
                self._switch_scheduled = False

    def _drain_switch(self, _):
        event = self._switch_event
        self._switch_event = None
        self._switch_scheduled = False
        if event is not None:
            self.call_handlers(event)

    def add_handler(self, handler):
        self.handlers.append(handler)

    def call_handlers(self, event_type):
        for handler in self.handlers:
            handler(event_type)
