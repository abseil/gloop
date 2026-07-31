"""Configuration for Gloop."""

HOST = "localhost"
PORT = 8080
DEBUG_MODE = True


def get_config():
  return {"host": HOST, "port": PORT, "debug": DEBUG_MODE}
