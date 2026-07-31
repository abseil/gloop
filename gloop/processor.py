"""Data processing for Gloop."""


def process_data(data, targets):
  """O(M+N) processing using a set for O(1) lookups."""
  data_set = set(data)
  results = []
  for target in targets:
    if target in data_set:
      results.append(target)
  return results
