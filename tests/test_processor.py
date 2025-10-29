"""
Tests for CSVProcessor class (integration tests).

Tests cover:
- Full pipeline: load -> filter -> analyze -> format
- Various parameter combinations
- Auto-detection of numeric columns
- Filter edge cases
- Output format switching
- Error handling in full pipeline
"""

import pytest
import json
from csv_processor.processor import CSVProcessor


class TestCSVProcessor:
    """Test suite for CSVProcessor integration."""

    @pytest.fixture
    def sample_csv_data(self):
        """Sample CSV data for testing."""
        return [
            {'name': 'Alice', 'age': '30', 'salary': '50000', 'city': 'NYC'},
            {'name': 'Bob', 'age': '25', 'salary': '45000', 'city': 'LA'},
            {'name': 'Charlie', 'age': '35', 'salary': '60000', 'city': 'Chicago'},
            {'name': 'Diana', 'age': '28', 'salary': '52000', 'city': 'Boston'},
            {'name': 'Eve', 'age': '32', 'salary': '58000', 'city': 'Seattle'}
        ]

    def test_process_basic(self, create_csv_file, sample_csv_data):
        """Should process CSV file with default settings."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        result = processor.process(csv_file)

        assert 'CSV Data Statistics' in result
        assert 'Column: age' in result
        assert 'Column: salary' in result

    def test_process_specific_columns(self, create_csv_file, sample_csv_data):
        """Should process only specified columns."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        result = processor.process(csv_file, columns=['age'])

        assert 'Column: age' in result
        assert 'Column: salary' not in result

    def test_process_with_filter(self, create_csv_file, sample_csv_data):
        """Should apply filter before calculating statistics."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        filter_spec = {'column': 'age', 'operator': '>', 'value': 30.0}
        result = processor.process(csv_file, filter_spec=filter_spec)

        # After filtering age > 30, should have Charlie (35) and Eve (32)
        assert 'CSV Data Statistics' in result

    def test_process_json_output(self, create_csv_file, sample_csv_data):
        """Should output statistics in JSON format."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        result = processor.process(csv_file, output_format='json')

        # Should be valid JSON
        parsed = json.loads(result)
        assert 'age' in parsed
        assert 'salary' in parsed
        assert 'mean' in parsed['age']

    def test_process_console_output(self, create_csv_file, sample_csv_data):
        """Should output statistics in console format."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        result = processor.process(csv_file, output_format='console')

        assert 'CSV Data Statistics' in result
        assert '=' in result
        assert 'Column:' in result

    def test_process_invalid_output_format(self, create_csv_file, sample_csv_data):
        """Should raise ValueError for invalid output format."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        with pytest.raises(ValueError) as exc_info:
            processor.process(csv_file, output_format='xml')

        assert 'invalid output format' in str(exc_info.value).lower()

    def test_process_missing_file(self):
        """Should raise FileNotFoundError for missing file."""
        processor = CSVProcessor()

        with pytest.raises(FileNotFoundError):
            processor.process('/nonexistent/file.csv')

    def test_process_filter_no_matches(self, create_csv_file, sample_csv_data):
        """Should return message when filter matches no rows."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        filter_spec = {'column': 'age', 'operator': '>', 'value': 100.0}
        result = processor.process(csv_file, filter_spec=filter_spec)

        assert 'No data matches the filter criteria' in result

    def test_process_filter_all_matches(self, create_csv_file, sample_csv_data):
        """Should process normally when filter matches all rows."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        filter_spec = {'column': 'age', 'operator': '>', 'value': 0.0}
        result = processor.process(csv_file, filter_spec=filter_spec)

        assert 'CSV Data Statistics' in result

    def test_process_auto_detect_numeric_columns(self, create_csv_file):
        """Should auto-detect numeric columns when none specified."""
        data = [
            {'name': 'Alice', 'age': '30', 'city': 'NYC'},
            {'name': 'Bob', 'age': '25', 'city': 'LA'}
        ]
        csv_file = create_csv_file(data)
        processor = CSVProcessor()

        result = processor.process(csv_file)

        # Should only process 'age' column (numeric)
        assert 'Column: age' in result
        assert 'Column: name' not in result
        assert 'Column: city' not in result

    def test_process_no_numeric_columns(self, create_csv_file):
        """Should raise ValueError when no numeric columns exist."""
        data = [
            {'name': 'Alice', 'city': 'NYC'},
            {'name': 'Bob', 'city': 'LA'}
        ]
        csv_file = create_csv_file(data)
        processor = CSVProcessor()

        with pytest.raises(ValueError) as exc_info:
            processor.process(csv_file)

        assert 'no numeric columns' in str(exc_info.value).lower()

    def test_process_with_multiple_filters(self, create_csv_file, sample_csv_data):
        """Should apply single filter correctly."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        # Filter for age > 28 and salary > 50000 (would need two separate processes)
        filter_spec = {'column': 'salary', 'operator': '>=', 'value': 52000.0}
        result = processor.process(csv_file, filter_spec=filter_spec)

        # Should include statistics for filtered data
        assert 'CSV Data Statistics' in result

    def test_validate_filter_spec_valid(self, create_csv_file, sample_csv_data):
        """Should accept valid filter specification."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        filter_spec = {'column': 'age', 'operator': '>', 'value': 30.0}

        # Should not raise exception
        result = processor.process(csv_file, filter_spec=filter_spec)
        assert result is not None

    def test_validate_filter_spec_missing_keys(self, create_csv_file, sample_csv_data):
        """Should raise ValueError when filter spec missing required keys."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        filter_spec = {'column': 'age', 'operator': '>'}  # Missing 'value'

        with pytest.raises(ValueError) as exc_info:
            processor.process(csv_file, filter_spec=filter_spec)

        assert 'must contain keys' in str(exc_info.value).lower()

    def test_validate_filter_spec_non_numeric_value(self, create_csv_file, sample_csv_data):
        """Should raise ValueError when filter value is not numeric."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        filter_spec = {'column': 'age', 'operator': '>', 'value': 'thirty'}

        with pytest.raises(ValueError) as exc_info:
            processor.process(csv_file, filter_spec=filter_spec)

        assert 'must be numeric' in str(exc_info.value).lower()

    def test_get_numeric_columns_mixed_data(self):
        """Should identify only numeric columns."""
        processor = CSVProcessor()
        data = {
            'name': ['Alice', 'Bob'],
            'age': [30.0, 25.0],
            'city': ['NYC', 'LA'],
            'salary': [50000.0, 45000.0]
        }

        numeric_cols = processor._get_numeric_columns(data)

        assert 'age' in numeric_cols
        assert 'salary' in numeric_cols
        assert 'name' not in numeric_cols
        assert 'city' not in numeric_cols

    def test_get_numeric_columns_all_numeric(self):
        """Should identify all columns when all are numeric."""
        processor = CSVProcessor()
        data = {
            'col1': [1.0, 2.0, 3.0],
            'col2': [4.0, 5.0, 6.0],
            'col3': [7.0, 8.0, 9.0]
        }

        numeric_cols = processor._get_numeric_columns(data)

        assert len(numeric_cols) == 3
        assert 'col1' in numeric_cols
        assert 'col2' in numeric_cols
        assert 'col3' in numeric_cols

    def test_get_numeric_columns_none_numeric(self):
        """Should return empty list when no numeric columns."""
        processor = CSVProcessor()
        data = {
            'name': ['Alice', 'Bob'],
            'city': ['NYC', 'LA']
        }

        numeric_cols = processor._get_numeric_columns(data)

        assert len(numeric_cols) == 0

    def test_get_numeric_columns_partial_numeric(self):
        """Should identify columns with at least one numeric value."""
        processor = CSVProcessor()
        data = {
            'mixed': [10.0, 'text', 20.0],
            'pure_text': ['a', 'b', 'c']
        }

        numeric_cols = processor._get_numeric_columns(data)

        assert 'mixed' in numeric_cols
        assert 'pure_text' not in numeric_cols

    def test_process_full_pipeline(self, create_csv_file, sample_csv_data):
        """Should execute complete pipeline: load, filter, analyze, format."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        filter_spec = {'column': 'age', 'operator': '>=', 'value': 30.0}
        result = processor.process(
            csv_file,
            columns=['age', 'salary'],
            filter_spec=filter_spec,
            output_format='json'
        )

        # Verify JSON output
        parsed = json.loads(result)
        assert 'age' in parsed
        assert 'salary' in parsed

        # Results should be based on filtered data (age >= 30)
        # Alice (30), Charlie (35), Eve (32)
        assert parsed['age']['count'] == 3

    def test_process_empty_after_filter(self, create_csv_file):
        """Should handle case where all data is filtered out."""
        data = [
            {'name': 'Alice', 'age': '30'},
            {'name': 'Bob', 'age': '25'}
        ]
        csv_file = create_csv_file(data)
        processor = CSVProcessor()

        filter_spec = {'column': 'age', 'operator': '>', 'value': 50.0}
        result = processor.process(csv_file, filter_spec=filter_spec)

        assert 'No data matches the filter criteria' in result

    def test_process_single_row_data(self, create_csv_file):
        """Should handle CSV with single data row."""
        data = [{'name': 'Alice', 'age': '30', 'salary': '50000'}]
        csv_file = create_csv_file(data)
        processor = CSVProcessor()

        result = processor.process(csv_file)

        assert 'CSV Data Statistics' in result
        assert 'Column: age' in result

    def test_process_components_initialized(self):
        """Should initialize all required components."""
        processor = CSVProcessor()

        assert processor.loader is not None
        assert processor.analyzer is not None
        assert processor.formatter is not None

    def test_process_nonexistent_column_in_filter(self, create_csv_file, sample_csv_data):
        """Should raise error when filtering on non-existent column."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        filter_spec = {'column': 'nonexistent', 'operator': '>', 'value': 10.0}

        with pytest.raises(ValueError) as exc_info:
            processor.process(csv_file, filter_spec=filter_spec)

        assert 'not found' in str(exc_info.value).lower()

    def test_process_nonexistent_column_in_columns(self, create_csv_file, sample_csv_data):
        """Should raise error when analyzing non-existent column."""
        csv_file = create_csv_file(sample_csv_data)
        processor = CSVProcessor()

        with pytest.raises(ValueError) as exc_info:
            processor.process(csv_file, columns=['nonexistent'])

        assert 'not found' in str(exc_info.value).lower()
