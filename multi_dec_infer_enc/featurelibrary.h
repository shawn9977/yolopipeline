#ifndef FEATURE_LIBRARY_H
#define FEATURE_LIBRARY_H

#include <vector>
#include <cstddef>
#include <mutex>

class FeatureLibrary {
public:
    FeatureLibrary();
    ~FeatureLibrary();

    /**
     * @brief 处理新特征：若库中存在相似特征则返回 ID，否则存入库并返回新 ID
     * @param new_data 指向特征数组的指针（长度必须为 FEATURE_DIM）
     * @return 匹配到的 ID 或新分配的 ID
     */
    int process_feature(const float* new_data);
    /**
     * @brief 获取特征库大小（线程安全）
     * @return 当前特征库中的特征数量
     */
    size_t get_size() const;

    static constexpr float THRESHOLD = 0.98f;
    static constexpr size_t FEATURE_DIM = 2048;

private:
    std::vector<std::vector<float>> library;
    mutable std::mutex library_mutex;

    void normalize(std::vector<float>& v);
    float calculate_cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);
};

#endif
