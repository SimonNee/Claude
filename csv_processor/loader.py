"""
CSV Data Loader

Loads CSV files and converts them into a dictionary structure for processing.
"""

import csv
from typing import Dict, List


class CSVDataLoader:
    """Loads CSV files and converts numeric strings to appropriate types."""

    def load(self, file_path: str) -> Dict[str, List]:
        """
        Load CSV file and return data as a dictionary.

        Args:
            file_path: Path to the CSV file

        Returns:
            Dictionary with column names as keys and lists of values

        Raises:
            FileNotFoundError: If the file doesn't exist
            ValueError: If the CSV file is empty or malformed
        """
        try:
            with open(file_path, 'r', encoding='utf-8') as file:
                reader = csv.DictReader(file)

                # Initialize dictionary with empty lists for each column
                data = {}
                fieldnames = reader.fieldnames

                if not fieldnames:
                    raise ValueError("CSV file has no headers")

                for field in fieldnames:
                    data[field] = []

                # Read rows and populate data
                row_count = 0
                for row in reader:
                    for field in fieldnames:
                        value = row.get(field, '')
                        data[field].append(self._convert_value(value))
                    row_count += 1

                if row_count == 0:
                    raise ValueError("CSV file contains no data rows")

                return data

        except FileNotFoundError:
            raise FileNotFoundError(f"CSV file not found: {file_path}")
        except csv.Error as e:
            raise ValueError(f"Error reading CSV file: {e}")

    def _convert_value(self, value: str):
        """
        Convert string value to appropriate type (float if numeric, else string).

        Args:
            value: String value from CSV

        Returns:
            Float if the value is numeric, otherwise the original string
        """
        if not value:
            return value

        try:
            # Try to convert to float
            return float(value)
        except ValueError:
            # If conversion fails, return as string
            return value
