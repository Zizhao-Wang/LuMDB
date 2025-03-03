import re

def process_file(input_file_path):
    # Open the original file and read the content
    with open(input_file_path, 'r', errors='ignore') as f:
        file_content = f.read()

    # Use regular expressions to split content by spaces and detect alphanumeric strings
    extracted_text = re.findall(r'[a-zA-Z0-9]+(?:\s+[a-zA-Z0-9]+)*', file_content)

    # Join the extracted text into lines and write it to a new file
    new_file_path = input_file_path + 'new'
    with open(new_file_path, 'w') as new_file:
        new_file.write('\n'.join(extracted_text))

    return new_file_path


# Example usage:
input_file_path = '/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/prismdb_scripts/10B_prismdb_zipf_hot/prismdb_10B_val_128_zipf1.3memory_budget2G.log'  # Replace with your actual file path
new_file = process_file(input_file_path)
print(f"Processed file saved at: {new_file}")
