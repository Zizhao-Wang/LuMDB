# LuMDB

Hello, welcome! 👋 This is the official repository of **LuMDB**. **LuMDB (i.e., <u>L</u>ow <u>U</u>nleashes <u>M</u>ore)** is the first **LSM-based KV store** that uses the variance of data access frequencies to represent and exploit workload skewness.

## Table of Contents
- [Background](#background)
- [Install](#install)
- [Usage](#usage)
- [Related projects](#related-projects)
- [Maintainers](#maintainers)
- [Contributing](#contributing)
- [License](#license)


## Background
LuMDB is an novel LSM-based key-value store designed to ultimately write amplification caused by frequent compactions through the following key optimizations:

1. **Workload Skewness Detection**: Automatically detects ***workload skewness*** to decide whether to apply hot-cold separation and to set appropriate thresholds for identifying hot data.

2. **Two-Phase Partitioning Leveling (2PPL)**: Implements a methodology that stores cold data using separation, automatically configuring partition ranges based on ***memtable*** size and ***workload skewness***. Additionally, a Tiering strategy manages and stores the remaining hot data when separation is applied. In scenarios where hot-cold separation is not suitable, LuMDB utilizes the 2PPL approach to store all data effectively.

Extensive experiments have demonstrated that LuMDB can reduce write amplification by up to **8.1×**, approaching its minimum theoretical limit, and increase read throughput by up to **3.7×** in the YCSB benchmark. 

For a more comprehensive understanding of the technical trade-offs in LuMDB, please refer to the blog below:

- [Why LSM?](https://zizhao-wang.github.io//posts/2024/12/Why-LSM/) 

## Install

1. **Clone the Repository**

   Start by cloning the official LuMDB repository to your local machine:

   ```sh
   git clone https://github.com/yourusername/LuMDB.git
   cd LuMDB
2. **Build the Project**

    Use Cmake to build the project:
   ```sh
    mkdir build
    cd build
    cmake ..
    make
3. **Run the db_bench**
   ```sh
    make test
## Usage

Since LuMDB is implemented based on LevelDB, it maintains the same external interfaces, allowing for seamless integration into existing projects. Below is an example of how to use LuMDB in a C++ project:

    
```cpp
#include "LuMDB/db.h"
int main() {
    lummdb::DB* db;
    lummdb::Options options;
    options.create_if_missing = true;

    // Open the database
    lummdb::Status status = lummdb::DB::Open(options, "/path/to/db", &db);
    assert(status.ok());

    // Use the database (interfaces are identical to LevelDB)
    status = db->Put(lummdb::WriteOptions(), "key", "value");
    assert(status.ok());

    std::string value;
    status = db->Get(lummdb::ReadOptions(), "key", &value);
    assert(status.ok());
    std::cout << "Retrieved value: " << value << std::endl;

    // Close the database
    delete db;
    return 0;
}
```

## Related projects

- [LevelDB](https://github.com/google/leveldb) - A fast key-value storage library written at Google that provides an ordered mapping from string keys to string values.
- [RocksDB](https://github.com/facebook/rocksdb) - A high-performance embedded database for key-value data, optimized for fast storage.

## Maintainers

[@Zizhao-Wang](https://github.com/Zizhao-Wang).

## Contributing

Feel free to dive in! [Open an issue](https://github.com/Zizhao-Wang/LuMDB/issues/new) or submit PRs.


## License

[MIT](LICENSE) © Richard Littauer
