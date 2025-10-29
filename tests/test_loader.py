"""
Tests for CSVDataLoader class.

Tests cover:
- Normal CSV loading
- Type conversion (strings to floats)
- Empty files
- Header-only files
- Missing files
- Malformed CSV
- Mixed data types
- Empty strings
- Encoding issues
"""

import pytest
import csv
from csv_processor.loader import CSVDataLoader


class TestCSVDataLoader:
    """Test suite for CSVDataLoader."""

    def test_load_valid_csv(self, create_csv_file, sample_csv_data):
        """Should successfully load a valid CSV file."""
        csv_file = create_csv_file(sample_csv_data)
        loader = CSVDataLoader()

        data = loader.load(csv_file)

        assert 'name' in data
        assert 'age' in data
        assert 'salary' in data
        assert len(data['name']) == 5
        assert data['name'][0] == 'Alice'

    def test_load_converts_numeric_strings(self, create_csv_file, sample_csv_data):
        """Should convert numeric strings to floats."""
        csv_file = create_csv_file(sample_csv_data)
        loader = CSVDataLoader()

        data = loader.load(csv_file)

        # Numeric columns should be converted to floats
        assert isinstance(data['age'][0], float)
        assert isinstance(data['salary'][0], float)
        assert data['age'][0] == 30.0
        assert data['salary'][0] == 50000.0

        # String columns should remain strings
        assert isinstance(data['name'][0], str)
        assert data['name'][0] == 'Alice'

    def test_load_missing_file(self):
        """Should raise FileNotFoundError for missing file."""
        loader = CSVDataLoader()

        with pytest.raises(FileNotFoundError) as exc_info:
            loader.load('/nonexistent/file.csv')

        assert 'not found' in str(exc_info.value).lower()

    def test_load_empty_file(self, temp_csv_file):
        """Should raise ValueError for completely empty file."""
        # Create empty file
        open(temp_csv_file, 'w').close()

        loader = CSVDataLoader()

        with pytest.raises(ValueError) as exc_info:
            loader.load(temp_csv_file)

        assert 'no headers' in str(exc_info.value).lower()

    def test_load_header_only_file(self, temp_csv_file):
        """Should raise ValueError for file with only headers."""
        with open(temp_csv_file, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['name', 'age', 'salary'])

        loader = CSVDataLoader()

        with pytest.raises(ValueError) as exc_info:
            loader.load(temp_csv_file)

        assert 'no data rows' in str(exc_info.value).lower()

    def test_load_mixed_columns(self, create_csv_file):
        """Should handle mixed numeric and string columns."""
        data = [
            {'id': '1', 'name': 'Alice', 'age': '30', 'city': 'NYC', 'score': '95.5'},
            {'id': '2', 'name': 'Bob', 'age': '25', 'city': 'LA', 'score': '87.3'}
        ]
        csv_file = create_csv_file(data)
        loader = CSVDataLoader()

        result = loader.load(csv_file)

        # Numeric columns converted
        assert isinstance(result['id'][0], float)
        assert isinstance(result['age'][0], float)
        assert isinstance(result['score'][0], float)

        # String columns remain strings
        assert isinstance(result['name'][0], str)
        assert isinstance(result['city'][0], str)

    def test_load_with_empty_strings(self, create_csv_file):
        """Should handle empty string values."""
        data = [
            {'name': 'Alice', 'age': '30', 'notes': ''},
            {'name': '', 'age': '25', 'notes': 'some note'},
            {'name': 'Charlie', 'age': '', 'notes': ''}
        ]
        csv_file = create_csv_file(data)
        loader = CSVDataLoader()

        result = loader.load(csv_file)

        # Empty strings should remain empty strings
        assert result['notes'][0] == ''
        assert result['name'][1] == ''
        assert result['age'][2] == ''

    def test_load_with_special_characters(self, create_csv_file):
        """Should handle special characters in data."""
        data = [
            {'name': 'José', 'city': 'São Paulo', 'amount': '100.50'},
            {'name': 'François', 'city': 'Paris', 'amount': '200.75'}
        ]
        csv_file = create_csv_file(data)
        loader = CSVDataLoader()

        result = loader.load(csv_file)

        assert result['name'][0] == 'José'
        assert result['city'][0] == 'São Paulo'
        assert isinstance(result['amount'][0], float)

    def test_convert_value_integers(self):
        """Should convert integer strings to floats."""
        loader = CSVDataLoader()

        assert loader._convert_value('42') == 42.0
        assert loader._convert_value('0') == 0.0
        assert loader._convert_value('-10') == -10.0

    def test_convert_value_floats(self):
        """Should convert float strings to floats."""
        loader = CSVDataLoader()

        assert loader._convert_value('3.14') == 3.14
        assert loader._convert_value('0.5') == 0.5
        assert loader._convert_value('-2.5') == -2.5

    def test_convert_value_strings(self):
        """Should keep non-numeric strings as strings."""
        loader = CSVDataLoader()

        assert loader._convert_value('hello') == 'hello'
        assert loader._convert_value('test123') == 'test123'
        assert loader._convert_value('') == ''

    def test_convert_value_empty_string(self):
        """Should handle empty strings correctly."""
        loader = CSVDataLoader()

        result = loader._convert_value('')
        assert result == ''
        assert isinstance(result, str)

    def test_load_single_row(self, create_csv_file):
        """Should handle CSV with single data row."""
        data = [{'name': 'Alice', 'age': '30'}]
        csv_file = create_csv_file(data)
        loader = CSVDataLoader()

        result = loader.load(csv_file)

        assert len(result['name']) == 1
        assert result['name'][0] == 'Alice'
        assert result['age'][0] == 30.0

    def test_load_large_numbers(self, create_csv_file):
        """Should handle large numeric values."""
        data = [
            {'id': '1', 'big_num': '9999999999.99'},
            {'id': '2', 'big_num': '1000000000000'}
        ]
        csv_file = create_csv_file(data)
        loader = CSVDataLoader()

        result = loader.load(csv_file)

        assert result['big_num'][0] == 9999999999.99
        assert result['big_num'][1] == 1000000000000.0

    def test_load_negative_numbers(self, create_csv_file):
        """Should handle negative numeric values."""
        data = [
            {'temp': '-5.5', 'balance': '-1000'},
            {'temp': '10.2', 'balance': '-500'}
        ]
        csv_file = create_csv_file(data)
        loader = CSVDataLoader()

        result = loader.load(csv_file)

        assert result['temp'][0] == -5.5
        assert result['balance'][0] == -1000.0

    def test_load_with_whitespace(self, temp_csv_file):
        """Should handle values with whitespace."""
        with open(temp_csv_file, 'w', newline='') as f:
            f.write('name,age\n')
            f.write(' Alice ,30\n')
            f.write('Bob, 25 \n')

        loader = CSVDataLoader()
        result = loader.load(temp_csv_file)

        # CSV reader should preserve whitespace
        assert ' Alice ' in result['name'][0] or 'Alice' in result['name'][0]

    def test_load_malformed_csv_missing_values(self, temp_csv_file):
        """Should handle CSV with missing values in rows."""
        with open(temp_csv_file, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['name', 'age', 'salary'])
            writer.writerow(['Alice', '30'])  # Missing salary
            writer.writerow(['Bob'])  # Missing age and salary

        loader = CSVDataLoader()
        result = loader.load(temp_csv_file)

        # Missing values should be None or empty strings (CSV DictReader returns None)
        # The _convert_value function will return them as is
        assert result['salary'][0] in ('', None)
        assert result['age'][1] in ('', None)
        assert result['salary'][1] in ('', None)
