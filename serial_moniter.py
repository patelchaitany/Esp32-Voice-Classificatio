import serial

ser = serial.serial_for_url(
    'rfc2217://localhost:4000',
    baudrate=115200,
    timeout=1
)


try:
    while True:
        line = ser.readline()  # reads until newline
        if line:
            print(line.decode(errors='replace').rstrip())
        else:
            # No data received within timeout
            pass
except KeyboardInterrupt:
    print("Stopping...")
finally:
    ser.close()
