"""
CSV Processor CLI

Command-line interface for processing CSV files.
"""

import argparse
import sys
from .processor import CSVProcessor


def parse_filter(filter_string: str) -> dict:
    """
    Parse filter string into filter specification.

    Args:
        filter_string: Filter in format "column operator value"
                      Example: "age > 30"

    Returns:
        Dictionary with column, operator, and value

    Raises:
        ValueError: If filter string is malformed
    """
    parts = filter_string.split()

    if len(parts) != 3:
        raise ValueError(
            "Filter must be in format: 'column operator value'\n"
            "Example: 'age > 30'"
        )

    column, operator, value_str = parts

    valid_operators = {'>', '<', '==', '>=', '<='}
    if operator not in valid_operators:
        raise ValueError(f"Operator must be one of: {valid_operators}")

    try:
        value = float(value_str)
    except ValueError:
        raise ValueError(f"Filter value must be numeric, got: {value_str}")

    return {
        'column': column,
        'operator': operator,
        'value': value
    }


def main():
    """Main entry point for CLI."""
    parser = argparse.ArgumentParser(
        description='Process CSV files and generate statistics',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s data.csv
  %(prog)s data.csv --columns age salary
  %(prog)s data.csv --filter "age > 30"
  %(prog)s data.csv --filter "salary >= 50000" --output json
        """
    )

    parser.add_argument(
        'file',
        help='Path to CSV file'
    )

    parser.add_argument(
        '--columns',
        nargs='+',
        help='Columns to analyze (default: all numeric columns)'
    )

    parser.add_argument(
        '--filter',
        help='Filter data (format: "column operator value")',
        metavar='FILTER'
    )

    parser.add_argument(
        '--output',
        choices=['console', 'json'],
        default='console',
        help='Output format (default: console)'
    )

    args = parser.parse_args()

    try:
        # Parse filter if provided
        filter_spec = None
        if args.filter:
            filter_spec = parse_filter(args.filter)

        # Process CSV
        processor = CSVProcessor()
        result = processor.process(
            file_path=args.file,
            columns=args.columns,
            filter_spec=filter_spec,
            output_format=args.output
        )

        print(result)
        return 0

    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Unexpected error: {e}", file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
