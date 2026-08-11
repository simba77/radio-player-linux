"""Radio Player — плеер интернет-радио для Linux."""

APP_ID = "radio-player"
APP_NAME = "Radio Player"
APP_VERSION = "0.1.0"

#: Имя сокета для single-instance lock и префикс D-Bus имени MPRIS.
IPC_SOCKET_NAME = "io.github.simba77.RadioPlayer"

__all__ = ["APP_ID", "APP_NAME", "APP_VERSION", "IPC_SOCKET_NAME"]
