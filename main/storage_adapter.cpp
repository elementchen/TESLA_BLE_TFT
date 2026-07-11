#include "storage_adapter.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

StorageAdapterImpl::StorageAdapterImpl() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash init failed (ret=%d), erasing and retrying...", ret);
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    initialized_ = true;
    ESP_LOGI(TAG, "NVS initialized");
}

StorageAdapterImpl::~StorageAdapterImpl() = default;

bool StorageAdapterImpl::load(const std::string &key, std::vector<uint8_t> &buffer) {
    if (!initialized_) return false;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open for load failed: %d", ret);
        return false;
    }

    size_t required_size = 0;
    ret = nvs_get_blob(handle, key.c_str(), nullptr, &required_size);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    buffer.resize(required_size);
    ret = nvs_get_blob(handle, key.c_str(), buffer.data(), &required_size);
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded key='%s', size=%u", key.c_str(), (unsigned)required_size);
    }
    return ret == ESP_OK;
}

bool StorageAdapterImpl::save(const std::string &key, const std::vector<uint8_t> &buffer) {
    if (!initialized_) return false;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open for save failed: %d", ret);
        return false;
    }

    ret = nvs_set_blob(handle, key.c_str(), buffer.data(), buffer.size());
    if (ret == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
        // Erase old entry and retry
        ESP_LOGW(TAG, "NVS full for '%s', erasing old entry...", key.c_str());
        nvs_erase_key(handle, key.c_str());
        nvs_commit(handle);
        ret = nvs_set_blob(handle, key.c_str(), buffer.data(), buffer.size());
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob for key='%s' failed: %d", key.c_str(), ret);
        nvs_close(handle);
        return false;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved key='%s', size=%u", key.c_str(), (unsigned)buffer.size());
    }
    return ret == ESP_OK;
}

bool StorageAdapterImpl::remove(const std::string &key) {
    if (!initialized_) return false;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return false;

    ret = nvs_erase_key(handle, key.c_str());
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return true; // 不存在也算成功
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret == ESP_OK;
}
