# LuMDB

Hello, welcome! 👋 This is the official repository of **LuMDB**. **LuMDB (i.e., <u>L</u>ow <u>U</u>nleashes <u>M</u>ore)** is the first **LSM-based KV store** that uses the variance of data access frequencies to represent and exploit workload skewness.

## Table of Contents
- [A brief introduction](#Introduction)
- [Install](#install)
- [Usage](#usage)
	- [Generator](#generator)
- [Badge](#badge)
- [Example Readmes](#example-readmes)
- [Related Efforts](#related-efforts)
- [Maintainers](#maintainers)
- [Contributing](#contributing)
- [License](#license)


## Introduction
Standard Readme started with the issue originally posed by [@maxogden](https://github.com/maxogden) over at [feross/standard](https://github.com/feross/standard) in [this issue](https://github.com/feross/standard/issues/141), about whether or not a tool to standardize readmes would be useful. A lot of that discussion ended up in [zcei's standard-readme](https://github.com/zcei/standard-readme/issues/1) repository. While working on maintaining the [IPFS](https://github.com/ipfs) repositories, I needed a way to standardize Readmes across that organization. This specification started as a result of that.

> Your documentation is complete when someone can use your module without ever
having to look at its code. This is very important. This makes it possible for
you to separate your module's documented interface from its internal
implementation (guts). This is good because it means that you are free to
change the module's internals as long as the interface remains the same.

> Remember: the documentation, not the code, defines what a module does.

~ [Ken Williams, Perl Hackers](http://mathforum.org/ken/perl_modules.html#document)

Writing READMEs is way too hard, and keeping them maintained is difficult. By offloading this process - making writing easier, making editing easier, making it clear whether or not an edit is up to spec or not - you can spend less time worrying about whether or not your initial documentation is good, and spend more time writing and using code.

By having a standard, users can spend less time searching for the information they want. They can also build tools to gather search terms from descriptions, to automatically run example code, to check licensing, and so on.

The goals for this repository are:

1. A well defined **specification**. This can be found in the [Spec document](spec.md). It is a constant work in progress; please open issues to discuss changes.
2. **An example README**. This Readme is fully standard-readme compliant, and there are more examples in the `example-readmes` folder.
3. A **linter** that can be used to look at errors in a given Readme. Please refer to the [tracking issue](https://github.com/RichardLitt/standard-readme/issues/5).
4. A **generator** that can be used to quickly scaffold out new READMEs. See [generator-standard-readme](https://github.com/RichardLitt/generator-standard-readme).
5. A **compliant badge** for users. See [the badge](#badge).

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

### Generator

To use the generator, look at [generator-standard-readme](https://github.com/RichardLitt/generator-standard-readme). There is a global executable to run the generator in that package, aliased as `standard-readme`.

## Badge

If your README is compliant with Standard-Readme and you're on GitHub, it would be great if you could add the badge. This allows people to link back to this Spec, and helps adoption of the README. The badge is **not required**.

[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square)](https://github.com/RichardLitt/standard-readme)

To add in Markdown format, use this code:

```
[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square)](https://github.com/RichardLitt/standard-readme)
```

## Related projects

- [LevelDB](https://github.com/google/leveldb) - A fast key-value storage library written at Google that provides an ordered mapping from string keys to string values.
- [RocksDB](https://github.com/facebook/rocksdb) - A high-performance embedded database for key-value data, optimized for fast storage.



## Maintainers

[@RichardLitt](https://github.com/RichardLitt).

## Contributing

Feel free to dive in! [Open an issue](https://github.com/RichardLitt/standard-readme/issues/new) or submit PRs.

Standard Readme follows the [Contributor Covenant](http://contributor-covenant.org/version/1/3/0/) Code of Conduct.

### Contributors

This project exists thanks to all the people who contribute. 
<a href="https://github.com/RichardLitt/standard-readme/graphs/contributors"><img src="https://opencollective.com/standard-readme/contributors.svg?width=890&button=false" /></a>


## License

[MIT](LICENSE) © Richard Littauer
