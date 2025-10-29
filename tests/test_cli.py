"""
Tests for CLI (main.py) functions.

Tests cover:
- Argument parsing
- Filter parsing
- Main function execution
- Error handling
- Invalid inputs
- Edge cases
"""

import pytest
import sys
from io import StringIO
from csv_processor.main import parse_filter, main


class TestParseFilter:
    """Test suite for parse_filter function."""

    def test_parse_filter_valid_greater_than(self):
        """Should parse valid greater than filter."""
        result = parse_filter("age > 30")

        assert result['column'] == 'age'
        assert result['operator'] == '>'
        assert result['value'] == 30.0

    def test_parse_filter_valid_less_than(self):
        """Should parse valid less than filter."""
        result = parse_filter("salary < 50000")

        assert result['column'] == 'salary'
        assert result['operator'] == '<'
        assert result['value'] == 50000.0

    def test_parse_filter_valid_equal(self):
        """Should parse valid equal filter."""
        result = parse_filter("age == 25")

        assert result['column'] == 'age'
        assert result['operator'] == '=='
        assert result['value'] == 25.0

    def test_parse_filter_valid_greater_equal(self):
        """Should parse valid greater or equal filter."""
        result = parse_filter("score >= 90")

        assert result['column'] == 'score'
        assert result['operator'] == '>='
        assert result['value'] == 90.0

    def test_parse_filter_valid_less_equal(self):
        """Should parse valid less or equal filter."""
        result = parse_filter("count <= 100")

        assert result['column'] == 'count'
        assert result['operator'] == '<='
        assert result['value'] == 100.0

    def test_parse_filter_float_value(self):
        """Should parse filter with float value."""
        result = parse_filter("temperature > 98.6")

        assert result['column'] == 'temperature'
        assert result['operator'] == '>'
        assert result['value'] == 98.6

    def test_parse_filter_negative_value(self):
        """Should parse filter with negative value."""
        result = parse_filter("balance > -100")

        assert result['column'] == 'balance'
        assert result['operator'] == '>'
        assert result['value'] == -100.0

    def test_parse_filter_column_with_underscore(self):
        """Should parse filter with underscore in column name."""
        result = parse_filter("user_age > 30")

        assert result['column'] == 'user_age'
        assert result['operator'] == '>'
        assert result['value'] == 30.0

    def test_parse_filter_too_few_parts(self):
        """Should raise ValueError for filter with too few parts."""
        with pytest.raises(ValueError) as exc_info:
            parse_filter("age >")

        assert 'format' in str(exc_info.value).lower()

    def test_parse_filter_too_many_parts(self):
        """Should raise ValueError for filter with too many parts."""
        with pytest.raises(ValueError) as exc_info:
            parse_filter("age > 30 extra")

        assert 'format' in str(exc_info.value).lower()

    def test_parse_filter_invalid_operator(self):
        """Should raise ValueError for invalid operator."""
        with pytest.raises(ValueError) as exc_info:
            parse_filter("age != 30")

        assert 'operator' in str(exc_info.value).lower()

    def test_parse_filter_non_numeric_value(self):
        """Should raise ValueError for non-numeric value."""
        with pytest.raises(ValueError) as exc_info:
            parse_filter("age > thirty")

        assert 'numeric' in str(exc_info.value).lower()

    def test_parse_filter_empty_string(self):
        """Should raise ValueError for empty filter string."""
        with pytest.raises(ValueError):
            parse_filter("")

    def test_parse_filter_no_spaces(self):
        """Should handle filter without spaces."""
        # Note: split() on "age>30" gives ['age>30'], which is 1 part, not 3
        with pytest.raises(ValueError):
            parse_filter("age>30")


class TestMainCLI:
    """Test suite for main CLI function."""

    def test_main_with_valid_file(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should process valid CSV file successfully."""
        csv_file = create_csv_file(sample_csv_data)

        # Mock sys.argv
        monkeypatch.setattr(sys, 'argv', ['csv_processor', csv_file])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        assert 'CSV Data Statistics' in captured.out

    def test_main_with_columns(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should process specific columns."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', ['csv_processor', csv_file, '--columns', 'age'])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        assert 'Column: age' in captured.out

    def test_main_with_filter(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should apply filter correctly."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', ['csv_processor', csv_file, '--filter', 'age > 30'])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        assert 'CSV Data Statistics' in captured.out or 'No data matches' in captured.out

    def test_main_with_json_output(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should output in JSON format."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', ['csv_processor', csv_file, '--output', 'json'])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        # Should be JSON format (has braces)
        assert '{' in captured.out

    def test_main_with_all_options(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should handle all options together."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', [
            'csv_processor',
            csv_file,
            '--columns', 'age', 'salary',
            '--filter', 'age >= 30',
            '--output', 'json'
        ])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        assert captured.out  # Should have output

    def test_main_missing_file(self, monkeypatch, capsys):
        """Should handle missing file gracefully."""
        monkeypatch.setattr(sys, 'argv', ['csv_processor', '/nonexistent/file.csv'])

        exit_code = main()

        assert exit_code == 1
        captured = capsys.readouterr()
        assert 'Error:' in captured.err or 'not found' in captured.err.lower()

    def test_main_invalid_filter(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should handle invalid filter format."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', [
            'csv_processor',
            csv_file,
            '--filter', 'invalid filter format'
        ])

        exit_code = main()

        assert exit_code == 1
        captured = capsys.readouterr()
        assert 'Error:' in captured.err

    def test_main_invalid_operator(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should handle invalid operator in filter."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', [
            'csv_processor',
            csv_file,
            '--filter', 'age != 30'
        ])

        exit_code = main()

        assert exit_code == 1
        captured = capsys.readouterr()
        assert 'Error:' in captured.err

    def test_main_invalid_column(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should handle invalid column name."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', [
            'csv_processor',
            csv_file,
            '--columns', 'nonexistent'
        ])

        exit_code = main()

        assert exit_code == 1
        captured = capsys.readouterr()
        assert 'Error:' in captured.err

    def test_main_no_arguments(self, monkeypatch, capsys):
        """Should show help when no arguments provided."""
        monkeypatch.setattr(sys, 'argv', ['csv_processor'])

        with pytest.raises(SystemExit):
            main()

    def test_main_multiple_columns(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should handle multiple columns."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', [
            'csv_processor',
            csv_file,
            '--columns', 'age', 'salary'
        ])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        assert 'Column: age' in captured.out
        assert 'Column: salary' in captured.out

    def test_main_filter_no_matches(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should handle filter with no matches."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', [
            'csv_processor',
            csv_file,
            '--filter', 'age > 100'
        ])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        assert 'No data matches' in captured.out

    def test_main_empty_file_error(self, temp_csv_file, monkeypatch, capsys):
        """Should handle empty file error."""
        # Create empty file
        open(temp_csv_file, 'w').close()

        monkeypatch.setattr(sys, 'argv', ['csv_processor', temp_csv_file])

        exit_code = main()

        assert exit_code == 1
        captured = capsys.readouterr()
        assert 'Error:' in captured.err

    def test_main_console_output_default(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should use console output by default."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', ['csv_processor', csv_file])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        assert '=' in captured.out  # Console format has separator lines

    def test_main_handles_unexpected_error(self, monkeypatch, capsys):
        """Should handle unexpected errors gracefully."""
        # Pass an invalid type for file to trigger unexpected error
        monkeypatch.setattr(sys, 'argv', ['csv_processor', None])

        # This should trigger an exception but be caught
        exit_code = main()

        # Should return error code
        assert exit_code == 1

    def test_parse_filter_whitespace(self):
        """Should handle extra whitespace in filter."""
        result = parse_filter("age  >  30")

        # Extra spaces should result in more than 3 parts after split
        # This should raise an error
        with pytest.raises(ValueError):
            parse_filter("age  >  30  extra")

    def test_main_filter_with_float(self, create_csv_file, sample_csv_data, monkeypatch, capsys):
        """Should handle filter with float value."""
        csv_file = create_csv_file(sample_csv_data)

        monkeypatch.setattr(sys, 'argv', [
            'csv_processor',
            csv_file,
            '--filter', 'salary >= 52000.5'
        ])

        exit_code = main()

        assert exit_code == 0
        captured = capsys.readouterr()
        assert captured.out  # Should have output

    def test_main_help_option(self, monkeypatch):
        """Should display help when --help is provided."""
        monkeypatch.setattr(sys, 'argv', ['csv_processor', '--help'])

        with pytest.raises(SystemExit) as exc_info:
            main()

        # --help should exit with 0
        assert exc_info.value.code == 0
