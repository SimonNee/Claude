"""
Tests for DataAnalyzer class.

Tests cover:
- Statistical calculations (mean, median, min, max, stdev)
- Filter operations with all operators (>, <, ==, >=, <=)
- Edge cases: single values, identical values, extreme values
- Non-existent columns
- No matches and all matches scenarios
- Non-numeric data handling
"""

import pytest
from csv_processor.analyzer import DataAnalyzer


class TestDataAnalyzer:
    """Test suite for DataAnalyzer."""

    @pytest.fixture
    def sample_data(self):
        """Sample data for testing."""
        return {
            'name': ['Alice', 'Bob', 'Charlie', 'Diana', 'Eve'],
            'age': [30.0, 25.0, 35.0, 28.0, 32.0],
            'salary': [50000.0, 45000.0, 60000.0, 52000.0, 58000.0]
        }

    def test_calculate_stats_single_column(self, sample_data):
        """Should calculate statistics for a single column."""
        analyzer = DataAnalyzer()

        stats = analyzer.calculate_stats(sample_data, ['age'])

        assert 'age' in stats
        assert stats['age']['mean'] == 30.0
        assert stats['age']['median'] == 30.0
        assert stats['age']['min'] == 25.0
        assert stats['age']['max'] == 35.0
        assert stats['age']['count'] == 5

    def test_calculate_stats_multiple_columns(self, sample_data):
        """Should calculate statistics for multiple columns."""
        analyzer = DataAnalyzer()

        stats = analyzer.calculate_stats(sample_data, ['age', 'salary'])

        assert 'age' in stats
        assert 'salary' in stats
        assert stats['age']['mean'] == 30.0
        assert stats['salary']['mean'] == 53000.0

    def test_calculate_stats_mean(self):
        """Should correctly calculate mean."""
        analyzer = DataAnalyzer()
        data = {'values': [10.0, 20.0, 30.0, 40.0, 50.0]}

        stats = analyzer.calculate_stats(data, ['values'])

        assert stats['values']['mean'] == 30.0

    def test_calculate_stats_median_odd_count(self):
        """Should correctly calculate median for odd number of values."""
        analyzer = DataAnalyzer()
        data = {'values': [10.0, 20.0, 30.0, 40.0, 50.0]}

        stats = analyzer.calculate_stats(data, ['values'])

        assert stats['values']['median'] == 30.0

    def test_calculate_stats_median_even_count(self):
        """Should correctly calculate median for even number of values."""
        analyzer = DataAnalyzer()
        data = {'values': [10.0, 20.0, 30.0, 40.0]}

        stats = analyzer.calculate_stats(data, ['values'])

        assert stats['values']['median'] == 25.0

    def test_calculate_stats_stdev(self):
        """Should correctly calculate standard deviation."""
        analyzer = DataAnalyzer()
        data = {'values': [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0]}

        stats = analyzer.calculate_stats(data, ['values'])

        # Standard deviation should be approximately 2.138 for this dataset
        assert abs(stats['values']['stdev'] - 2.138) < 0.01

    def test_calculate_stats_single_value(self):
        """Should handle single value (stdev = 0)."""
        analyzer = DataAnalyzer()
        data = {'values': [42.0]}

        stats = analyzer.calculate_stats(data, ['values'])

        assert stats['values']['mean'] == 42.0
        assert stats['values']['median'] == 42.0
        assert stats['values']['min'] == 42.0
        assert stats['values']['max'] == 42.0
        assert stats['values']['stdev'] == 0.0
        assert stats['values']['count'] == 1

    def test_calculate_stats_identical_values(self):
        """Should handle all identical values."""
        analyzer = DataAnalyzer()
        data = {'values': [5.0, 5.0, 5.0, 5.0, 5.0]}

        stats = analyzer.calculate_stats(data, ['values'])

        assert stats['values']['mean'] == 5.0
        assert stats['values']['median'] == 5.0
        assert stats['values']['min'] == 5.0
        assert stats['values']['max'] == 5.0
        assert stats['values']['stdev'] == 0.0

    def test_calculate_stats_extreme_values(self):
        """Should handle extreme numeric values."""
        analyzer = DataAnalyzer()
        data = {'values': [0.001, 1000000.0, 0.002, 999999.0]}

        stats = analyzer.calculate_stats(data, ['values'])

        assert stats['values']['min'] == 0.001
        assert stats['values']['max'] == 1000000.0

    def test_calculate_stats_negative_numbers(self):
        """Should handle negative numbers."""
        analyzer = DataAnalyzer()
        data = {'values': [-10.0, -5.0, 0.0, 5.0, 10.0]}

        stats = analyzer.calculate_stats(data, ['values'])

        assert stats['values']['mean'] == 0.0
        assert stats['values']['median'] == 0.0
        assert stats['values']['min'] == -10.0
        assert stats['values']['max'] == 10.0

    def test_calculate_stats_nonexistent_column(self, sample_data):
        """Should raise ValueError for non-existent column."""
        analyzer = DataAnalyzer()

        with pytest.raises(ValueError) as exc_info:
            analyzer.calculate_stats(sample_data, ['nonexistent'])

        assert 'not found' in str(exc_info.value).lower()

    def test_calculate_stats_no_numeric_values(self):
        """Should raise ValueError when column has no numeric values."""
        analyzer = DataAnalyzer()
        data = {'names': ['Alice', 'Bob', 'Charlie']}

        with pytest.raises(ValueError) as exc_info:
            analyzer.calculate_stats(data, ['names'])

        assert 'no numeric values' in str(exc_info.value).lower()

    def test_calculate_stats_mixed_types(self):
        """Should filter out non-numeric values and calculate stats."""
        analyzer = DataAnalyzer()
        data = {'mixed': [10.0, 'text', 20.0, '', 30.0]}

        stats = analyzer.calculate_stats(data, ['mixed'])

        # Should only use numeric values: 10, 20, 30
        assert stats['mixed']['count'] == 3
        assert stats['mixed']['mean'] == 20.0

    def test_filter_data_greater_than(self, sample_data):
        """Should filter data with > operator."""
        analyzer = DataAnalyzer()

        filtered = analyzer.filter_data(sample_data, 'age', '>', 30.0)

        # Should keep rows where age > 30: Charlie (35), Eve (32)
        assert len(filtered['age']) == 2
        assert 35.0 in filtered['age']
        assert 32.0 in filtered['age']

    def test_filter_data_less_than(self, sample_data):
        """Should filter data with < operator."""
        analyzer = DataAnalyzer()

        filtered = analyzer.filter_data(sample_data, 'age', '<', 30.0)

        # Should keep rows where age < 30: Bob (25), Diana (28)
        assert len(filtered['age']) == 2
        assert 25.0 in filtered['age']
        assert 28.0 in filtered['age']

    def test_filter_data_equal_to(self, sample_data):
        """Should filter data with == operator."""
        analyzer = DataAnalyzer()

        filtered = analyzer.filter_data(sample_data, 'age', '==', 30.0)

        # Should keep rows where age == 30: Alice
        assert len(filtered['age']) == 1
        assert filtered['age'][0] == 30.0
        assert filtered['name'][0] == 'Alice'

    def test_filter_data_greater_equal(self, sample_data):
        """Should filter data with >= operator."""
        analyzer = DataAnalyzer()

        filtered = analyzer.filter_data(sample_data, 'age', '>=', 30.0)

        # Should keep rows where age >= 30: Alice (30), Charlie (35), Eve (32)
        assert len(filtered['age']) == 3
        assert 30.0 in filtered['age']
        assert 35.0 in filtered['age']
        assert 32.0 in filtered['age']

    def test_filter_data_less_equal(self, sample_data):
        """Should filter data with <= operator."""
        analyzer = DataAnalyzer()

        filtered = analyzer.filter_data(sample_data, 'age', '<=', 30.0)

        # Should keep rows where age <= 30: Alice (30), Bob (25), Diana (28)
        assert len(filtered['age']) == 3
        assert 30.0 in filtered['age']
        assert 25.0 in filtered['age']
        assert 28.0 in filtered['age']

    def test_filter_data_no_matches(self, sample_data):
        """Should return empty data when no rows match filter."""
        analyzer = DataAnalyzer()

        filtered = analyzer.filter_data(sample_data, 'age', '>', 100.0)

        # All columns should exist but be empty
        assert 'age' in filtered
        assert 'name' in filtered
        assert len(filtered['age']) == 0
        assert len(filtered['name']) == 0

    def test_filter_data_all_matches(self, sample_data):
        """Should return all data when all rows match filter."""
        analyzer = DataAnalyzer()

        filtered = analyzer.filter_data(sample_data, 'age', '>', 0.0)

        # Should keep all rows
        assert len(filtered['age']) == 5
        assert len(filtered['name']) == 5

    def test_filter_data_preserves_all_columns(self, sample_data):
        """Should preserve all columns in filtered result."""
        analyzer = DataAnalyzer()

        filtered = analyzer.filter_data(sample_data, 'age', '>', 30.0)

        # All original columns should be present
        assert 'name' in filtered
        assert 'age' in filtered
        assert 'salary' in filtered

        # Filtered rows should match across columns
        assert len(filtered['name']) == len(filtered['age'])
        assert len(filtered['age']) == len(filtered['salary'])

    def test_filter_data_nonexistent_column(self, sample_data):
        """Should raise ValueError for non-existent column."""
        analyzer = DataAnalyzer()

        with pytest.raises(ValueError) as exc_info:
            analyzer.filter_data(sample_data, 'nonexistent', '>', 10.0)

        assert 'not found' in str(exc_info.value).lower()

    def test_filter_data_invalid_operator(self, sample_data):
        """Should raise ValueError for invalid operator."""
        analyzer = DataAnalyzer()

        with pytest.raises(ValueError) as exc_info:
            analyzer.filter_data(sample_data, 'age', '!=', 30.0)

        assert 'invalid operator' in str(exc_info.value).lower()

    def test_filter_data_skips_non_numeric(self):
        """Should skip non-numeric values when filtering."""
        analyzer = DataAnalyzer()
        data = {
            'id': [1.0, 2.0, 3.0, 4.0, 5.0],
            'mixed': [10.0, 'text', 20.0, '', 30.0]
        }

        filtered = analyzer.filter_data(data, 'mixed', '>', 15.0)

        # Should only match numeric values > 15: 20, 30
        assert len(filtered['id']) == 2

    def test_compare_greater_than(self):
        """Should correctly compare with > operator."""
        analyzer = DataAnalyzer()

        assert analyzer._compare(10.0, '>', 5.0) is True
        assert analyzer._compare(5.0, '>', 10.0) is False
        assert analyzer._compare(5.0, '>', 5.0) is False

    def test_compare_less_than(self):
        """Should correctly compare with < operator."""
        analyzer = DataAnalyzer()

        assert analyzer._compare(5.0, '<', 10.0) is True
        assert analyzer._compare(10.0, '<', 5.0) is False
        assert analyzer._compare(5.0, '<', 5.0) is False

    def test_compare_equal(self):
        """Should correctly compare with == operator."""
        analyzer = DataAnalyzer()

        assert analyzer._compare(5.0, '==', 5.0) is True
        assert analyzer._compare(5.0, '==', 10.0) is False

    def test_compare_greater_equal(self):
        """Should correctly compare with >= operator."""
        analyzer = DataAnalyzer()

        assert analyzer._compare(10.0, '>=', 5.0) is True
        assert analyzer._compare(5.0, '>=', 5.0) is True
        assert analyzer._compare(5.0, '>=', 10.0) is False

    def test_compare_less_equal(self):
        """Should correctly compare with <= operator."""
        analyzer = DataAnalyzer()

        assert analyzer._compare(5.0, '<=', 10.0) is True
        assert analyzer._compare(5.0, '<=', 5.0) is True
        assert analyzer._compare(10.0, '<=', 5.0) is False

    def test_compare_invalid_operator(self):
        """Should return False for invalid operator."""
        analyzer = DataAnalyzer()

        assert analyzer._compare(5.0, '!=', 10.0) is False
        assert analyzer._compare(5.0, 'invalid', 10.0) is False
