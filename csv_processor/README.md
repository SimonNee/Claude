# CSV Data Processor

A simple, easy-to-use CSV data processing tool for loading, analyzing, and formatting CSV data. Built with Python's standard library following KISS (Keep It Simple, Stupid) principles.

## Features

- Load CSV files with automatic type detection
- Calculate statistics (mean, median, min, max, standard deviation)
- Filter data based on conditions
- Output results in console or JSON format
- Pure Python - no external dependencies required

## Installation

No installation required! Just Python 3.8 or higher.

```bash
# Clone or copy the csv_processor directory
cd csv_processor
```

## Usage

### Command Line Interface

Basic usage:
```bash
python -m csv_processor.main data.csv
```

With specific columns:
```bash
python -m csv_processor.main data.csv --columns age salary
```

With filtering:
```bash
python -m csv_processor.main data.csv --filter "age > 30"
```

JSON output:
```bash
python -m csv_processor.main data.csv --filter "salary >= 50000" --output json
```

### Supported Filter Operators

- `>` - Greater than
- `<` - Less than
- `==` - Equal to
- `>=` - Greater than or equal to
- `<=` - Less than or equal to

### Python API

```python
from csv_processor import CSVProcessor

# Create processor
processor = CSVProcessor()

# Process with all defaults
result = processor.process('data.csv')
print(result)

# Process with filter
filter_spec = {
    'column': 'age',
    'operator': '>',
    'value': 30
}
result = processor.process(
    'data.csv',
    filter_spec=filter_spec,
    output_format='json'
)
print(result)

# Process specific columns
result = processor.process(
    'data.csv',
    columns=['age', 'salary']
)
print(result)
```

### Individual Components

```python
from csv_processor import CSVDataLoader, DataAnalyzer, OutputFormatter

# Load CSV
loader = CSVDataLoader()
data = loader.load('data.csv')

# Analyze data
analyzer = DataAnalyzer()
stats = analyzer.calculate_stats(data, ['age', 'salary'])

# Filter data
filtered = analyzer.filter_data(data, 'age', '>', 30)

# Format output
formatter = OutputFormatter()
console_output = formatter.format_console(stats)
json_output = formatter.format_json(stats)
```

## File Structure

```
csv_processor/
├── __init__.py       # Package initialization
├── loader.py         # CSVDataLoader - loads CSV files
├── analyzer.py       # DataAnalyzer - statistics and filtering
├── formatter.py      # OutputFormatter - output formatting
├── processor.py      # CSVProcessor - orchestrates workflow
├── main.py          # CLI entry point
├── sample_data.csv  # Sample CSV for testing
└── README.md        # This file
```

## Examples

### Sample Data (sample_data.csv)

```csv
name,age,salary,department
Alice,28,65000,Engineering
Bob,35,75000,Engineering
Carol,42,85000,Management
Dave,31,70000,Engineering
Eve,29,68000,Sales
Frank,45,90000,Management
Grace,33,72000,Sales
```

### Example 1: Basic Statistics

```bash
python -m csv_processor.main sample_data.csv
```

Output:
```
============================================================
CSV Data Statistics
============================================================

Column: age
----------------------------------------
  Count:      7
  Mean:       34.71
  Median:     33.00
  Min:        28.00
  Max:        45.00
  Std Dev:    6.50

Column: salary
----------------------------------------
  Count:      7
  Mean:       75000.00
  Median:     72000.00
  Min:        65000.00
  Max:        90000.00
  Std Dev:    9201.45
============================================================
```

### Example 2: Filter by Age

```bash
python -m csv_processor.main sample_data.csv --filter "age > 30"
```

### Example 3: JSON Output

```bash
python -m csv_processor.main sample_data.csv --output json
```

Output:
```json
{
  "age": {
    "mean": 34.71,
    "median": 33.0,
    "min": 28.0,
    "max": 45.0,
    "stdev": 6.50,
    "count": 7
  },
  "salary": {
    "mean": 75000.0,
    "median": 72000.0,
    "min": 65000.0,
    "max": 90000.0,
    "stdev": 9201.45,
    "count": 7
  }
}
```

## Error Handling

The tool provides clear error messages for common issues:

- File not found
- Empty CSV files
- Invalid filter syntax
- Non-numeric columns
- Invalid operators

## Design Principles

This project follows KISS principles:

- **Simple**: Standard library only, no external dependencies
- **Readable**: Clear variable names and function signatures
- **Modular**: Each component has a single responsibility
- **Explicit**: Type hints and docstrings throughout
- **Small**: Functions under 50 lines, classes under 100 lines

## Requirements

- Python 3.8 or higher
- No external dependencies (uses only standard library)

## License

This is a learning project - use freely for educational purposes.
