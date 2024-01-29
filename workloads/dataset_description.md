# 数据集描述

该数据集由模拟的键值存储操作组成，具体特征如下：

- **行数**：该数据集包含10,000,000行数据。
- **列数**：每行数据由5列组成，分别是：

  - `Key`: 表示键值对的键（key），是根据Zipf分布生成的整数，范围从1到数据集的大小。
  - `key_length`: 键的长度，表示键字符串的字符数，是一个整数，根据Generalized Extreme Value分布生成。
  - `Value`: 表示键值对的值（value），是一个随机生成的字符串，其长度由`val_length`列给出。
  - `val_length`: 值的长度，表示值字符串的字符数，是一个整数，根据Generalized Pareto分布生成。
  - `Operation`: 表示对键值对执行的操作类型，它是一个类别型列，可能的值有`GET`、`PUT`和`DELETE`。

## 列描述

- `Key`: 键的标识符，代表键值对中的"键"部分。每个键是唯一的，其分布遵循Zipf定律，模拟现实世界中某些键被访问得更频繁的情况。
- `key_length`: 键字符串的长度（以字符计）。这个数字是根据Generalized Extreme Value分布生成的，反映了不同键可能具有不同的长度。
- `Value`: 键值对中的"值"部分。它是一个由小写字母随机组成的字符串，其长度由相应的`val_length`列决定。
- `val_length`: 值字符串的长度（以字符计）。这个数字是根据Generalized Pareto分布生成的，反映了不同值的大小变化。
- `Operation`: 对键值对执行的操作类型。它是一个类别型列，包含`GET`、`PUT`和`DELETE`三种操作，其分布由设置的概率决定，模拟了键值存储中各种操作的相对频率。

## 数据集来源
- [1] [Atikoglu, Berk, et al. "Workload analysis of a large-scale key-value store."](https://d1wqtxts1xzle7.cloudfront.net/79389509/Workload_analysis_of_a_large-scale_key-v20220122-13612-2f4q3v.pdf?1642899357=&response-content-disposition=inline%3B+filename%3DWorkload_analysis_of_a_large_scale_key_v.pdf&Expires=1704305115&Signature=K6Hf2HYwo1H8Fu~n4gjCdsyaXKeE~OmX3GWGjx08ImCS8tHUc4ridqdms~skDNXkhmI9rfheVm1mfanP9NNlhhc-UK2eq0Piq2Si4REv-3rH~Nvb7KN7t-JzX40q3T71jzKpo7Mf2FES-WJG2CIh4seU~v3tdKgdKHnStFcikGrTSa2WLYvjy71xaQBMyRXNGsrwB~JaxmxrgBWlneSsWKG4KAMnk4hH-pESrXa-RkC3YNtmtHk~bEuqHA8BvlSvgratPBkdVudLNRRFB-pOj-ZuM6kM7sDQPsVS68ia0mncDAECaEtjNJB2bmTd3ydkknUuHz5l7ZZiNH-Mh~DBFw__&Key-Pair-Id=APKAJLOHF5GGSLRBV4ZA) Proceedings of the 12th ACM SIGMETRICS/PERFORMANCE joint international conference on Measurement and Modeling of Computer Systems. 2012.
