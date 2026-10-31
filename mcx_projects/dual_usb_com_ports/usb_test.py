
import serial
import time

with serial.Serial("COM20", 115200, timeout=2) as ser:
    time.sleep(0.5)

    message = b"Hello MCXN236!\r\n"

    ser.reset_input_buffer()
    print("Sending: ", repr(message))

    ser.write(message)
    ser.flush()

    received = ser.read(len(message))

    print("Received:", repr(received))

    if received == message:
        print("PASS: USB CDC echo working!")
    else:
        print("FAIL: Echo did not match")
