"""Watch an ESP32 serial port without sending a reset command."""

import argparse
import sys
import time

import serial
from esp_pylib.serial_reset import hard_reset


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3", help="serial port (default: COM3)")
    parser.add_argument("--baud", type=int, default=115200, help="baud rate (default: 115200)")
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="only listen; do not reset the board when the port opens",
    )
    args = parser.parse_args()

    print(
        f"Listening on {args.port} at {args.baud} baud. "
        f"{'Not resetting the board. ' if args.no_reset else 'Resetting the board now. '}"
        "Press Ctrl+C here to stop.",
        flush=True,
    )

    reset_pending = not args.no_reset
    try:
        while True:
            connection = None
            try:
                # Set modem-control lines before opening so opening the port does not
                # deliberately pulse RTS/DTR to reset the board.
                connection = serial.Serial(port=None, baudrate=args.baud, timeout=0.25)
                connection.dtr = False
                connection.rts = False
                connection.port = args.port
                connection.open()
                print(f"Connected to {args.port}.", flush=True)
                if reset_pending:
                    reset_pending = False
                    connection.reset_input_buffer()
                    hard_reset(connection)

                while True:
                    data = connection.read(256)
                    if data:
                        sys.stdout.write(data.decode("utf-8", errors="replace"))
                        sys.stdout.flush()
            except (serial.SerialException, OSError) as exc:
                print(f"\n{args.port} disconnected or unavailable ({exc}); retrying...", flush=True)
                time.sleep(0.75)
            finally:
                if connection is not None and connection.is_open:
                    connection.close()
    except KeyboardInterrupt:
        print("\nStopped.", flush=True)
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
