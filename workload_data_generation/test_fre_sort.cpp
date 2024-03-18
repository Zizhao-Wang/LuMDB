#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
#include <string>

// 定义用于存储键和频率的pair的类型
typedef std::pair<int64_t, int> KeyFreqPair;

// 定义比较函数，用于排序
bool compareFreq(const KeyFreqPair& a, const KeyFreqPair& b) {
    return a.second > b.second; // 降序排序
}

int main() {

    std::string inputFileName = "/home/jeff-wang/workloads/zipf1.02_keys10.0B.csv";
    std::string outputFileName = "/home/jeff-wang/workloads/cpp_test.csv";
    std::map<int64_t, int> frequencies;

    std::ifstream inputFile(inputFileName);
    std::ofstream outputFile(outputFileName);

    if (!inputFile.is_open() || !outputFile.is_open()) {
        std::cerr << "Error opening file." << std::endl;
        return 1;
    }

    std::string line;
    // Skip header
    std::getline(inputFile, line);

    while (std::getline(inputFile, line)) {
        std::stringstream ss(line);
        std::string keyString;
        std::getline(ss, keyString, ',');

        int64_t key = std::stoll(keyString);
        frequencies[key]++;
    }

    // 将map的内容复制到vector中以便排序
    std::vector<KeyFreqPair> freqVector(frequencies.begin(), frequencies.end());

    // 使用自定义比较函数对vector进行排序
    std::sort(freqVector.begin(), freqVector.end(), compareFreq);

    // 写入到输出文件
    outputFile << "Key,Frequency\n";
    for (const auto& pair : freqVector) {
        outputFile << pair.first << "," << pair.second << "\n";
    }

    std::cout << "Frequencies have been written to " << outputFileName << std::endl;

    return 0;
}
