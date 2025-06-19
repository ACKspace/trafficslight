=== TrafficSlight ===

Small traffic lights made from scrap and garbage. See http://trafficslight.ack.space/

This code uses ESP-NOW to broadcast lightweight messages between an arbitrary amount of traffic lights (up to 255 in theory).
Current implementation tries to hand over the "green light" by releasing the intersection after a "standby" response was given on a red light.

It has 3 speeds (time between red and release intersection) and allows German "pre green" mode (red+amber).
A "hidden" mode is available when both speed-inputs are grounded; it will broadcast blink to all traffic lights are blinking in sync.
