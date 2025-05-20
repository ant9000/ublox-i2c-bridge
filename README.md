# ublox-i2c-bridge

This little tool enables standard serial communication for U-Blox GPS modules connected via I²C.

### Why?

To enable read-only communication I've used the excellent [gpsd-i2c](https://github.com/MaffooClock/gpsd-i2c), which was perfect for its use case. But when I had to achieve a very low latency for feeding GPS time to a local NTP server, it turned out that Python resource usage was making the system behave badly under load - which prompted me to rewrite an extremely stripped-down C version. Moreover, my module was only accessible via I²C: in order configure it at all, bidirectional communication was needed.

### How this works

This utility continuously reads a data stream from a particular I²C device and dumps it to `stdout`. On the other side of the communication, it reads `stdin` for UBX, NMEA or RTCM messages, and handles them over to the I²C device. I've chosen to ignore any data validation for the device-provided data: I expect the final consumer of the stream to be responsible for that. For writing to the GPS module, though, some minimal parsing is needed in order to write messages with a single I²C transmission.

Socat is responsible for creating the virtual serial port device attached to `stdin` and `stdout` of the C program. Any tool or program (such as GPSD) that can read from a serial device can then make use of this virtual serial device.

This utility includes a systemd service file to automatically run at boot-up, which is designed to make the existing gpsd.service depend on it; the GPSD service is supposed to start after this one, which is necessary because the virtual serial port that GPSD will be expecting to read from won't exist until this service starts.

### Requirements

You'll need a compiler and socat to make this utility work:

1. [socat](http://www.dest-unreach.org/socat/)

If you're using any current version of Debian or Ubuntu, or any distribution based on either of those, this will be easy:
```bash
sudo apt install build-essential socat
```

### Install

1. Clone this repository and compile the program:
   ```bash
   cd /opt
   git clone https://github.com/ant9000/ublox-i2c-bridge
   cd ublox-i2c-bridge
   gcc -o ublox-i2c-bridge ublox-i2c-bridge.c
   ```

2. Test the service (use the I²C bus and address of your GPS module):
   ```bash
   /opt/ublox-i2c-bridge/ublox-i2c-bridge /dev/i2c-0 0x42
   ```
   If everything is correct, you should immediately see a bunch of messages in the console.  Just <kbd>Ctrl</kbd>+<kbd>C</kbd> to stop, and then continue with the remaining steps below.

   If not, you'll need to resolve whatever errors you see before proceeding.

3. Setup the systemd service (use the I²C bus and address of your GPS module):
   ```bash
   cd /opt/ublox-i2c-bridge
   cp ublox-i2c-bridge.service /etc/systemd/system/
   mkdir /etc/systemd/system/ublox-i2c-bridge.service.d/
   printf "[Service]\nEnvironment=I2C_DEV=0 I2C_ADDRESS=0x42\n" > \
     /etc/systemd/system/ublox-i2c-bridge.service.d/override.conf
   ```

4. Start the service automatically on boot:
   ```bash
   systemctl enable --now ublox-i2c-bridge.service
   ```

Of course, you don't have to install this into `/opt` -- anywhere you prefer is fine, just be sure to update the `WorkingDirectory=` line in the override file.

If the service starts, then you should see a new virtual serial port in `/dev/gpsd0`.  You can change the name of this device, if necessary, by adding a `TTY_DEV` in the override file.

> [!TIP]
> Don't forget to execute `systemctl daemon-reload` after editing systemd files.

You can monitor the status with:
```bash
journalctl -fu ublox-i2c-bridge.service
```


### Reconfigure GPSD

Now that socat is running our utility and redirecting its output to `/dev/gpsd0` (or whatever device you may have changed it to), we can configure GPSD to read from it.

First, stop GPSD if it is running (assumes systemd):
```bash
systemctl stop gpsd
```

**For Debian/Ubuntu users:** If you installed GPSD with `apt`, you might be able to do this one of two ways:

1. Run:
   ```bash
   dpkg-reconfigure gpsd
   ```
   ...to be prompted for configuration options.  Just specify `/dev/gpsd0` when prompted for the device.

2. Alternatively, you should have a file at `/etc/default/gpsd` that looks something like this:
   ```bash
   START_DAEMON="true"
   GPSD_OPTIONS="-n"
   #DEVICES="/dev/ttyAMA0"
   DEVICES="/dev/gpsd0"
   USBAUTO="false"
   GPSD_SOCKET="/var/run/gpsd.sock"
   ```

**For other systems**, these configuration options might be found in:
- `/etc/sysconfig`
- `/lib/systemd/system/gpsd.service`
- `/etc/systemd/system/gpsd.service`

Once you've re-configured GPSD, you can attempt to restart it:
```bash
systemctl start gpsd
```

Then check the logs to see that it's actually running:
```bash
journalctl -fu gpsd
```

You can then execute either `gpsmon` or `cgps -s` to view live data from GPSD. As a final test, check bidirectional communication by resetting the module with:

```bash
ubxtool ::/dev/gpsd0 -p RESET
```

## Credits

This tool is born out of [gpsd-i2c](https://github.com/MaffooClock/gpsd-i2c): I have copied the service structure with the `socat` trick, and most of the ideas come from Matthew Clark excellent work.
