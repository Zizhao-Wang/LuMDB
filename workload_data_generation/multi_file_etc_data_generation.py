import pandas as pd
import json
import os
from multiprocessing import Pool
from tqdm import tqdm

def label_keys(key_list, hot_percentage=0.2):
    key_list = list(key_list)
    key_counts = {key: key_list.count(key) for key in set(key_list)}
    hot_threshold = int(len(set(key_list)) * hot_percentage)
    hot_keys = set(sorted(key_counts, key=key_counts.get, reverse=True)[:hot_threshold])

    return {key: 1 if key in hot_keys else 0 for key in set(key_list)}

def process_file(file_name):
    data = pd.read_csv(f"data/{file_name}")
    key_list = data['Key'].values.tolist()

    labels_at_each_step = {}

    # Keep track of all keys seen so far
    keys_seen_so_far = set()

    for i in range(50, len(key_list) + 50, 50):
        current_key_list = key_list[:i]
        keys_seen_so_far.update(current_key_list)
        labels = label_keys(current_key_list)

        # Only include keys in the labels that have been seen so far
        labels_at_this_step = {key: labels[key] for key in key_list[i-50:i] if key in keys_seen_so_far}
        labels_at_each_step[i] = labels_at_this_step

    with open(f"label/{file_name.replace('.csv', '.json')}", 'w') as json_file:
        json.dump(labels_at_each_step, json_file)

def init_process_file(file_name):
    try:
        process_file(file_name)
    except Exception as e:
        print(f"Error processing file {file_name}: {e}")

def main(k):
    files = [f for f in os.listdir("data") if f.endswith('.csv')]

    with Pool(processes=k) as pool:
        for _ in tqdm(pool.imap(init_process_file, files), total=len(files)):
            pass

if __name__ == "__main__":
    K = 4  # Adjust this value based on your system's capabilities and the number of files
    main(K)

