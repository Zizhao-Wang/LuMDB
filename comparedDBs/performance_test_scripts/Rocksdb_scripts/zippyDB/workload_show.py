import pandas as pd
import matplotlib.pyplot as plt

# Open the file in read-only mode using 'r'
file_path = '/mnt/workloads/zippyDB_key_file2.csv'  # Make sure this is the correct file path

# Load the CSV file into a DataFrame
df = pd.read_csv(file_path)

# Extract the 'Key' column and plot its distribution
plt.figure(figsize=(10, 6))
plt.hist(df['Key'], bins=50, edgecolor='black')
plt.title('Key Distribution')
plt.xlabel('Key Value')
plt.ylabel('Frequency')
plt.grid(True)
plt.show()
