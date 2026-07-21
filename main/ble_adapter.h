#pragma once

#include "adapters.h"
#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"

class BleAdapterImpl : public TeslaBLE::BleAdapter {
public:
    using DataCallback   = std::function<void(const std::vector<uint8_t> &)>;
    using StatusCallback = std::function<void(bool)>;

    BleAdapterImpl();
    ~BleAdapterImpl() override;

    void connect(const std::string &address) override;
    void disconnect() override;
    bool write(const std::vector<uint8_t> &data) override;

    void init(const std::string &vin);
    void deinit();
    void set_data_callback(DataCallback cb)   { data_cb_ = std::move(cb); }
    void set_status_callback(StatusCallback cb) { status_cb_ = std::move(cb); }
    void process();
    bool is_connected() const { return conn_handle_ != BLE_HS_CONN_HANDLE_NONE; }

    // NimBLE event handlers
    int on_gap(struct ble_gap_event *e);
    int on_disc_svc(uint16_t ch, const struct ble_gatt_error *err,
                    const struct ble_gatt_svc *svc);
    int on_disc_chr(uint16_t ch, const struct ble_gatt_error *err,
                    const struct ble_gatt_chr *chr);
    int on_disc_dsc(uint16_t ch, const struct ble_gatt_error *err,
                    uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc);

private:
    static constexpr const char *TAG = "BleAdapter";
    enum class State : uint8_t { IDLE, SCANNING, CONNECTING, DISCOVERING, CONNECTED };

    State state_ = State::IDLE;
    std::string expected_name_;
    uint16_t conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
    uint16_t svc_start_ = 0, svc_end_ = 0;
    uint16_t write_handle_ = 0, read_handle_ = 0, cccd_handle_ = 0;
    int svc_retries_ = 0, chr_retries_ = 0;
    bool disc_svcs_ = true; // true=discovering services, false=discovering chrs
    uint32_t deferred_scan_at_ = 0;
    uint32_t deferred_disc_at_ = 0;

    static constexpr size_t BLK = 18;
    static constexpr int MAX_RETRY = 5;
    std::queue<std::vector<uint8_t>> wq_;
    std::mutex wq_mtx_;
    std::vector<uint8_t> rx_buf_;
    size_t rx_expect_ = 0;
    DataCallback   data_cb_;
    StatusCallback status_cb_;
    bool nimble_init_ = false, synced_ = false;

    void start_scan();
    void do_connect(const ble_addr_t &addr);
    void start_disc();
    void enable_notif();
    void send_one();
    void on_rx(const uint8_t *d, size_t n);
    void set_state(State s);

    static std::string ble_name(const std::string &vin);

    // NimBLE callbacks (static)
    static BleAdapterImpl *inst_;
    static int _gap(struct ble_gap_event *e, void *);
    static int _disc_svc(uint16_t ch, const struct ble_gatt_error *err,
                         const struct ble_gatt_svc *svc, void *);
    static int _disc_chr(uint16_t ch, const struct ble_gatt_error *err,
                         const struct ble_gatt_chr *chr, void *);
    static int _disc_dsc(uint16_t ch, const struct ble_gatt_error *err,
                          uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *);
    static int _cccd_cb(uint16_t ch, const struct ble_gatt_error *err,
                        struct ble_gatt_attr *attr, void *arg);
    static int _mtu_cb(uint16_t conn_handle, const struct ble_gatt_error *err,
                        uint16_t mtu, void *arg);
};
