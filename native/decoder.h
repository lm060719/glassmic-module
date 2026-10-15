#pragma once
#include <cstdint>
#include <memory>
#include <string>
namespace gm {
class Decoder {
public:
    Decoder(); ~Decoder();
    bool open(const std::string& path);
    uint64_t read(float* out,uint64_t frames);
    bool seek(uint64_t frame);
    uint64_t length() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
