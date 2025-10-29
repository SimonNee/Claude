"""
Pytest configuration and shared fixtures for CSV processor tests.
"""

import pytest
import os
import tempfile
import csv
from pathlib import Path


@pytest.fixture
def temp_csv_file():
    """Create a temporary CSV file and clean up after test."""
    temp_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    temp_file.close()

    yield temp_file.name

    # Cleanup
    if os.path.exists(temp_file.name):
        os.unlink(temp_file.name)


@pytest.fixture
def sample_csv_data():
    """Sample CSV data for testing."""
    return [
        {'name': 'Alice', 'age': '30', 'salary': '50000'},
        {'name': 'Bob', 'age': '25', 'salary': '45000'},
        {'name': 'Charlie', 'age': '35', 'salary': '60000'},
        {'name': 'Diana', 'age': '28', 'salary': '52000'},
        {'name': 'Eve', 'age': '32', 'salary': '58000'}
    ]


@pytest.fixture
def create_csv_file(temp_csv_file):
    """Factory fixture to create CSV files with custom data."""
    def _create(data, headers=None):
        if not data:
            # Create empty file
            open(temp_csv_file, 'w').close()
            return temp_csv_file

        # Use first row keys as headers if not provided
        if headers is None:
            headers = list(data[0].keys())

        with open(temp_csv_file, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=headers)
            writer.writeheader()
            writer.writerows(data)

        return temp_csv_file

    return _create


@pytest.fixture
def test_data_dir():
    """Return path to test data directory."""
    return Path(__file__).parent / 'test_data'
