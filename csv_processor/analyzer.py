"""
Data Analyzer

Performs statistical analysis and filtering operations on CSV data.
"""

import statistics
from typing import Dict, List


class DataAnalyzer:
    """Analyzes data and provides statistical summaries and filtering."""

    def calculate_stats(self, data: Dict[str, List], columns: List[str]) -> Dict:
        """
        Calculate statistics for specified columns.

        Args:
            data: Dictionary with column names as keys and lists of values
            columns: List of column names to analyze

        Returns:
            Dictionary with statistics for each column

        Raises:
            ValueError: If column doesn't exist or contains non-numeric data
        """
        results = {}

        for column in columns:
            if column not in data:
                raise ValueError(f"Column '{column}' not found in data")

            values = data[column]

            # Filter out non-numeric values
            numeric_values = [v for v in values if isinstance(v, (int, float))]

            if not numeric_values:
                raise ValueError(f"Column '{column}' contains no numeric values")

            results[column] = {
                'mean': statistics.mean(numeric_values),
                'median': statistics.median(numeric_values),
                'min': min(numeric_values),
                'max': max(numeric_values),
                'stdev': statistics.stdev(numeric_values) if len(numeric_values) > 1 else 0.0,
                'count': len(numeric_values)
            }

        return results

    def filter_data(self, data: Dict[str, List], column: str,
                   operator: str, value: float) -> Dict[str, List]:
        """
        Filter data based on a condition.

        Args:
            data: Dictionary with column names as keys and lists of values
            column: Column name to filter on
            operator: Comparison operator ('>', '<', '==', '>=', '<=')
            value: Value to compare against

        Returns:
            Filtered dictionary with same structure as input

        Raises:
            ValueError: If column doesn't exist or operator is invalid
        """
        if column not in data:
            raise ValueError(f"Column '{column}' not found in data")

        valid_operators = {'>', '<', '==', '>=', '<='}
        if operator not in valid_operators:
            raise ValueError(f"Invalid operator '{operator}'. Must be one of {valid_operators}")

        # Get indices of rows that match the filter condition
        matching_indices = []
        column_values = data[column]

        for i, val in enumerate(column_values):
            if not isinstance(val, (int, float)):
                continue

            if self._compare(val, operator, value):
                matching_indices.append(i)

        # Build filtered data with matching rows
        filtered_data = {}
        for col_name, col_values in data.items():
            filtered_data[col_name] = [col_values[i] for i in matching_indices]

        return filtered_data

    def _compare(self, val: float, operator: str, target: float) -> bool:
        """
        Compare two values using the specified operator.

        Args:
            val: Value to compare
            operator: Comparison operator
            target: Target value

        Returns:
            Boolean result of comparison
        """
        if operator == '>':
            return val > target
        elif operator == '<':
            return val < target
        elif operator == '==':
            return val == target
        elif operator == '>=':
            return val >= target
        elif operator == '<=':
            return val <= target
        return False
