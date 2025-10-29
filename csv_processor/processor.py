"""
CSV Processor

Coordinates the CSV processing pipeline: loading, analyzing, and formatting data.
"""

from typing import Optional, Dict, List
from .loader import CSVDataLoader
from .analyzer import DataAnalyzer
from .formatter import OutputFormatter


class CSVProcessor:
    """Orchestrates CSV data processing workflow."""

    def __init__(self):
        """Initialize processor with required components."""
        self.loader = CSVDataLoader()
        self.analyzer = DataAnalyzer()
        self.formatter = OutputFormatter()

    def process(self, file_path: str,
                columns: Optional[List[str]] = None,
                filter_spec: Optional[Dict] = None,
                output_format: str = 'console') -> str:
        """
        Process CSV file with optional filtering and format output.

        Args:
            file_path: Path to CSV file
            columns: List of columns to analyze (None = all numeric columns)
            filter_spec: Optional filter dictionary with keys:
                        'column', 'operator', 'value'
            output_format: Output format ('console' or 'json')

        Returns:
            Formatted output string

        Raises:
            ValueError: If parameters are invalid or processing fails
        """
        # Load data
        data = self.loader.load(file_path)

        # Apply filter if specified
        if filter_spec:
            self._validate_filter_spec(filter_spec)
            data = self.analyzer.filter_data(
                data,
                filter_spec['column'],
                filter_spec['operator'],
                filter_spec['value']
            )

            # Check if filter resulted in empty dataset
            if not data or not any(data.values()):
                return "No data matches the filter criteria"

        # Determine which columns to analyze
        if columns is None:
            # Auto-detect numeric columns
            columns = self._get_numeric_columns(data)

        if not columns:
            raise ValueError("No numeric columns found to analyze")

        # Calculate statistics
        stats = self.analyzer.calculate_stats(data, columns)

        # Format output
        if output_format == 'json':
            return self.formatter.format_json(stats)
        elif output_format == 'console':
            return self.formatter.format_console(stats)
        else:
            raise ValueError(f"Invalid output format: {output_format}. "
                           f"Must be 'console' or 'json'")

    def _validate_filter_spec(self, filter_spec: Dict) -> None:
        """
        Validate filter specification.

        Args:
            filter_spec: Filter dictionary to validate

        Raises:
            ValueError: If filter specification is invalid
        """
        required_keys = {'column', 'operator', 'value'}
        if not all(key in filter_spec for key in required_keys):
            raise ValueError(f"Filter spec must contain keys: {required_keys}")

        if not isinstance(filter_spec['value'], (int, float)):
            raise ValueError("Filter value must be numeric")

    def _get_numeric_columns(self, data: Dict[str, List]) -> List[str]:
        """
        Identify columns that contain numeric data.

        Args:
            data: Dictionary of column data

        Returns:
            List of column names with numeric data
        """
        numeric_columns = []

        for column, values in data.items():
            # Check if column has at least one numeric value
            has_numeric = any(isinstance(v, (int, float)) for v in values)
            if has_numeric:
                numeric_columns.append(column)

        return numeric_columns
