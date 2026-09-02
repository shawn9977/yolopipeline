#include "featurelibrary.h"
#include <cmath>
#include <numeric>
#include <cstdio>
#include <algorithm>

FeatureLibrary::FeatureLibrary() {
    // 根据预期规模可以预留内存以减少 reallocation
    library.reserve(10000);
}

FeatureLibrary::~FeatureLibrary() = default;

int FeatureLibrary::process_feature(const float* new_data) {
    std::vector<float> new_vec(FEATURE_DIM);
    std::copy(new_data, new_data + FEATURE_DIM, new_vec.begin());
    normalize(new_vec);

    std::lock_guard<std::mutex> lock(library_mutex);

    for (size_t i = 0; i < library.size(); ++i) {
        float similarity = calculate_cosine_similarity(new_vec, library[i]);
        if (similarity >= THRESHOLD) {
            printf("Feature library size =%ld; Recall id = %zu; similarity =%f \n", library.size(), i, similarity);
            return static_cast<int>(i);
        }
    }

    //printf("Insert a new item into Feature library! \n");
    library.push_back(std::move(new_vec));
    return static_cast<int>(library.size() - 1);
}

size_t FeatureLibrary::FeatureLibrary::get_size() const {
    std::lock_guard<std::mutex> lock(library_mutex);
    return library.size();
}

void FeatureLibrary::normalize(std::vector<float>& v) {
    float norm_sq = std::inner_product(v.begin(), v.end(), v.begin(), 0.0f);
    float norm = std::sqrt(norm_sq);
    if (norm > 1e-8f) {
        float inv_norm = 1.0f / norm;
        for (float& x : v) {
            x *= inv_norm;
        }
    }
}

float FeatureLibrary::calculate_cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
    return std::inner_product(a.begin(), a.end(), b.begin(), 0.0f);
}

