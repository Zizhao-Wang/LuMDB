import pandas as pd

csv_files = '/wzz/SHCDB/workloads/etc_data.csv'

data = pd.read_csv(csv_files)


keys = data['Key']
# print(keys)
frequency_of_keys =  keys.value_counts()

# print(frequency_of_keys)
labels=[]

for index,value in frequency_of_keys.items():
    # print(index, value)
    if value >= 10:
        labels.append(index)

labeled=[]
for i,v in keys.items():
    if v in labels:
        labeled.append(1)
    else:
        labeled.append(0)

data['labels'] = labeled

data.to_csv(csv_files,index=False)

print(labels)
