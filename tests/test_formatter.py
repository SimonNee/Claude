"""
Tests for OutputFormatter class.

Tests cover:
- Console formatting
- JSON formatting
- Empty statistics
- Large and small numbers
- JSON validity
- Multiple columns
- Formatting edge cases
"""

import pytest
import json
from csv_processor.formatter import OutputFormatter


class TestOutputFormatter:
    """Test suite for OutputFormatter."""

    @pytest.fixture
    def sample_stats(self):
        """Sample statistics for testing."""
        return {
            'age': {
                'mean': 30.0,
                'median': 30.0,
                'min': 25.0,
                'max': 35.0,
                'stdev': 3.54,
                'count': 5
            },
            'salary': {
                'mean': 53000.0,
                'median': 52000.0,
                'min': 45000.0,
                'max': 60000.0,
                'stdev': 5477.23,
                'count': 5
            }
        }

    def test_format_console_single_column(self):
        """Should format single column statistics for console."""
        formatter = OutputFormatter()
        stats = {
            'age': {
                'mean': 30.0,
                'median': 30.0,
                'min': 25.0,
                'max': 35.0,
                'stdev': 3.54,
                'count': 5
            }
        }

        output = formatter.format_console(stats)

        assert 'CSV Data Statistics' in output
        assert 'Column: age' in output
        assert 'Mean:       30.00' in output
        assert 'Median:     30.00' in output
        assert 'Min:        25.00' in output
        assert 'Max:        35.00' in output
        assert 'Std Dev:    3.54' in output
        assert 'Count:      5' in output

    def test_format_console_multiple_columns(self, sample_stats):
        """Should format multiple column statistics for console."""
        formatter = OutputFormatter()

        output = formatter.format_console(sample_stats)

        assert 'Column: age' in output
        assert 'Column: salary' in output
        assert 'Mean:       30.00' in output
        assert 'Mean:       53000.00' in output

    def test_format_console_empty_stats(self):
        """Should handle empty statistics gracefully."""
        formatter = OutputFormatter()

        output = formatter.format_console({})

        assert output == "No statistics to display"

    def test_format_console_structure(self, sample_stats):
        """Should include proper formatting structure."""
        formatter = OutputFormatter()

        output = formatter.format_console(sample_stats)

        # Check for separators
        assert '=' * 60 in output
        assert '-' * 40 in output

        # Check structure
        lines = output.split('\n')
        assert len(lines) > 10  # Should have multiple lines

    def test_format_console_large_numbers(self):
        """Should format large numbers correctly."""
        formatter = OutputFormatter()
        stats = {
            'revenue': {
                'mean': 1000000.50,
                'median': 999999.99,
                'min': 500000.00,
                'max': 1500000.00,
                'stdev': 250000.00,
                'count': 10
            }
        }

        output = formatter.format_console(stats)

        assert 'Mean:       1000000.50' in output
        assert 'Max:        1500000.00' in output

    def test_format_console_small_numbers(self):
        """Should format small numbers correctly."""
        formatter = OutputFormatter()
        stats = {
            'ratio': {
                'mean': 0.123,
                'median': 0.115,
                'min': 0.001,
                'max': 0.999,
                'stdev': 0.234,
                'count': 100
            }
        }

        output = formatter.format_console(stats)

        assert 'Mean:       0.12' in output
        assert 'Min:        0.00' in output
        assert 'Max:        1.00' in output

    def test_format_console_zero_values(self):
        """Should format zero values correctly."""
        formatter = OutputFormatter()
        stats = {
            'score': {
                'mean': 0.0,
                'median': 0.0,
                'min': 0.0,
                'max': 0.0,
                'stdev': 0.0,
                'count': 1
            }
        }

        output = formatter.format_console(stats)

        assert 'Mean:       0.00' in output
        assert 'Std Dev:    0.00' in output

    def test_format_console_negative_numbers(self):
        """Should format negative numbers correctly."""
        formatter = OutputFormatter()
        stats = {
            'temperature': {
                'mean': -5.5,
                'median': -4.0,
                'min': -15.0,
                'max': 5.0,
                'stdev': 7.2,
                'count': 20
            }
        }

        output = formatter.format_console(stats)

        assert 'Mean:       -5.50' in output
        assert 'Min:        -15.00' in output

    def test_format_json_single_column(self):
        """Should format single column statistics as JSON."""
        formatter = OutputFormatter()
        stats = {
            'age': {
                'mean': 30.0,
                'median': 30.0,
                'min': 25.0,
                'max': 35.0,
                'stdev': 3.54,
                'count': 5
            }
        }

        output = formatter.format_json(stats)

        # Should be valid JSON
        parsed = json.loads(output)
        assert 'age' in parsed
        assert parsed['age']['mean'] == 30.0
        assert parsed['age']['count'] == 5

    def test_format_json_multiple_columns(self, sample_stats):
        """Should format multiple column statistics as JSON."""
        formatter = OutputFormatter()

        output = formatter.format_json(sample_stats)

        parsed = json.loads(output)
        assert 'age' in parsed
        assert 'salary' in parsed
        assert parsed['age']['mean'] == 30.0
        assert parsed['salary']['mean'] == 53000.0

    def test_format_json_structure(self, sample_stats):
        """Should create properly indented JSON."""
        formatter = OutputFormatter()

        output = formatter.format_json(sample_stats)

        # Should have indentation (indent=2)
        assert '  ' in output
        # Should be valid JSON
        parsed = json.loads(output)
        assert isinstance(parsed, dict)

    def test_format_json_empty_stats(self):
        """Should format empty statistics as valid JSON."""
        formatter = OutputFormatter()

        output = formatter.format_json({})

        parsed = json.loads(output)
        assert parsed == {}

    def test_format_json_preserves_numeric_precision(self):
        """Should preserve numeric precision in JSON."""
        formatter = OutputFormatter()
        stats = {
            'precise': {
                'mean': 123.456789,
                'median': 98.765432,
                'min': 0.123456,
                'max': 999.999999,
                'stdev': 45.678901,
                'count': 100
            }
        }

        output = formatter.format_json(stats)

        parsed = json.loads(output)
        assert parsed['precise']['mean'] == 123.456789
        assert parsed['precise']['median'] == 98.765432

    def test_format_json_all_stat_fields(self, sample_stats):
        """Should include all statistic fields in JSON."""
        formatter = OutputFormatter()

        output = formatter.format_json(sample_stats)

        parsed = json.loads(output)

        for column in ['age', 'salary']:
            assert 'mean' in parsed[column]
            assert 'median' in parsed[column]
            assert 'min' in parsed[column]
            assert 'max' in parsed[column]
            assert 'stdev' in parsed[column]
            assert 'count' in parsed[column]

    def test_format_json_is_valid_json(self, sample_stats):
        """Should produce valid JSON that can be parsed."""
        formatter = OutputFormatter()

        output = formatter.format_json(sample_stats)

        # Should not raise exception
        parsed = json.loads(output)
        assert isinstance(parsed, dict)

    def test_format_console_decimal_formatting(self):
        """Should format decimals to 2 places consistently."""
        formatter = OutputFormatter()
        stats = {
            'value': {
                'mean': 12.3,
                'median': 12.34567,
                'min': 10.1,
                'max': 15.99999,
                'stdev': 2.123456,
                'count': 50
            }
        }

        output = formatter.format_console(stats)

        # All should be formatted to 2 decimal places
        assert 'Mean:       12.30' in output
        assert 'Median:     12.35' in output
        assert 'Max:        16.00' in output

    def test_format_console_count_no_decimals(self, sample_stats):
        """Should format count as integer without decimals."""
        formatter = OutputFormatter()

        output = formatter.format_console(sample_stats)

        # Count should not have decimal places
        assert 'Count:      5' in output
        assert 'Count:      5.00' not in output
