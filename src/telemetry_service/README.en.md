# Telemetry Service

[简体中文](README.md) | [English](README.en.md)

Positioning and telemetry reporting module. Reads NMEA positioning data from a GNSS module over a serial port, parses location information, and publishes it via MQTT. Also listens for iceoryx2 alert events and publishes danger alerts to a separate MQTT topic.

## Features

- Reads NMEA 0183 sentences from a GNSS module over a UART serial port
- Parses `$GPGGA` and `$GPRMC` sentences to extract latitude, longitude, altitude, speed, track, and other positioning data
- Publishes position data as JSON to a configurable MQTT topic
- Listens for iceoryx2 alert events and publishes alert information as JSON to a separate MQTT topic
- Supports MQTT QoS 0/1/2 and the Retain flag

## Data Flow

```text
GNSS Serial Port → NmeaPositionParser → PositionFix → MQTT Position JSON
                                                          ↓
iceoryx2 Alert Event → EventListener → MQTT Alert JSON
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-d, --device <path>` | GNSS serial device (default: `/dev/ttyS9`) |
| `--baud-rate <rate>` | Serial baud rate (default: 9600) |
| `--mqtt-host <host>` | MQTT broker host (default: `127.0.0.1`) |
| `--mqtt-port <1-65535>` | MQTT broker port (default: 1883) |
| `--mqtt-client-id <id>` | MQTT client ID |
| `--mqtt-topic <topic>` | Position data MQTT topic (default: `signlang/position`) |
| `--alert-event-service <name>` | iceoryx2 alert event service name |
| `--alert-mqtt-topic <topic>` | Alert MQTT topic (default: `signlang/alert`) |
| `--mqtt-username <user>` | MQTT username |
| `--mqtt-password <pass>` | MQTT password |
| `--mqtt-keep-alive <sec>` | MQTT keep-alive interval (default: 30 s) |
| `--mqtt-qos <0|1|2>` | MQTT QoS level (default: 0) |
| `--mqtt-retain` | Enable the MQTT Retain flag |

## NMEA Parsing

`NmeaPositionParser` uses the [minmea](https://github.com/kosma/minmea) library to parse NMEA 0183 sentences. Supported sentence types:

- `$GPGGA` — GPS Fix Data (time, latitude, longitude, altitude, satellite count, HDOP)
- `$GPRMC` — Recommended Minimum Specific GNSS Data (time, latitude, longitude, speed, track)

Parse statuses include: Empty, Invalid Sentence, Unsupported Sentence, Parse Failed, No Fix, Invalid Coordinates, Valid Fix.

## MQTT Publishing

- Position JSON includes: `timestamp`, `latitude`, `longitude`, `altitude`, `speed`, `track`, `satellites`, `hdop`
- Alert JSON includes: `timestamp`, `alert_type`, `message`
- A fixed-capacity queue (64 entries) buffers outgoing messages; up to 32 messages are drained every 20 ms
- Two independent `boost::asio::io_context` instances: one for serial I/O, one for the MQTT connection

## Dependencies

- Boost.Asio / Boost.MQTT5 / Boost.JSON
- minmea (NMEA 0183 parsing)
- iceoryx2 (event)
