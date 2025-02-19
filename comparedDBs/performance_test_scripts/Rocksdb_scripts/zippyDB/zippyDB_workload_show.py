import pandas as pd
import matplotlib.pyplot as plt

# Open the file in read-only mode
file_path = '/mnt/workloads/zippyDB_key_file2.csv'  # Adjust the file path if necessary

# Load the CSV file into a DataFrame
df = pd.read_csv(file_path)

# Count the occurrences of each key
key_counts = df['Key'].value_counts()

# Plot the key frequency distribution as a line plot
plt.figure(figsize=(10, 6))
key_counts.plot(kind='line', marker='o', linestyle='-', color='b', grid=True)
plt.title('Key Distribution Frequency')
plt.xlabel('Key')
plt.ylabel('Frequency')
plt.grid(True)
plt.show()

# Save the plot as an image in the current directory
plt.savefig('./zippydb_key_distribution.png')

# Display a message indicating the image is saved
print("Plot saved as 'zippydb_key_distribution.png'")

# Display the top 10 keys and their frequencies
top_10_keys = key_counts.head(10)
print("Top 10 keys and their frequencies:")
print(top_10_keys)
