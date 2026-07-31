"""Loader for Gloop configuration."""

import json


def load_config(filepath):
  with open(filepath, 'r') as f:
    content = f.read()
  # Fixed: Replaced eval() with json.loads() for safety.
  return json.loads(content)
