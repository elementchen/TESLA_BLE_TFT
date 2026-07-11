#pragma once

#include "adapters.h"
#include <string>
#include <vector>

class StorageAdapterImpl : public TeslaBLE::StorageAdapter {
public:
    StorageAdapterImpl();
    ~StorageAdapterImpl() override;

    bool load(const std::string &key, std::vector<uint8_t> &buffer) override;
    bool save(const std::string &key, const std::vector<uint8_t> &buffer) override;
    bool remove(const std::string &key) override;

private:
    static constexpr const char *TAG = "StorageAdapter";
    static constexpr const char *NVS_NAMESPACE = "tesla_ble";
    bool initialized_ = false;
};
