"""
Simple test script to demonstrate the Python API usage.
Run from parent directory: python -m csv_processor.test_example
"""

from .processor import CSVProcessor

def main():
    processor = CSVProcessor()

    print("Test 1: Basic processing")
    print("-" * 60)
    result = processor.process('csv_processor/sample_data.csv')
    print(result)
    print()

    print("Test 2: With filter")
    print("-" * 60)
    filter_spec = {
        'column': 'age',
        'operator': '>',
        'value': 30
    }
    result = processor.process(
        'csv_processor/sample_data.csv',
        filter_spec=filter_spec
    )
    print(result)
    print()

    print("Test 3: JSON output")
    print("-" * 60)
    result = processor.process(
        'csv_processor/sample_data.csv',
        columns=['salary'],
        output_format='json'
    )
    print(result)

if __name__ == '__main__':
    main()
