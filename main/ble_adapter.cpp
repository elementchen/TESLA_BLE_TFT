#include "ble_adapter.h"
#include "esp_log.h"
#include "mbedtls/sha1.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "esp_nimble_hci.h"
#include <cstdio>
#include <cstring>

// ─── Tesla BLE 128-bit UUIDs ────────────────────────────────────
// BLE transmits 128-bit UUIDs in little-endian byte order.
// UUID: 00000211-b2d1-43f0-9b88-960cebf8b91e
// LE bytes: 1e b9 f8 eb 0c 96 88 9b f0 43 d1 b2 11 02 00 00
#define TV8(v...) { v }

static const ble_uuid128_t UUID_SVC = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = TV8(0x1e,0xb9,0xf8,0xeb,0x0c,0x96,0x88,0x9b,0xf0,0x43,0xd1,0xb2,0x11,0x02,0x00,0x00)
};
static const ble_uuid128_t UUID_WR = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = TV8(0x1e,0xb9,0xf8,0xeb,0x0c,0x96,0x88,0x9b,0xf0,0x43,0xd1,0xb2,0x12,0x02,0x00,0x00)
};
static const ble_uuid128_t UUID_RD = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = TV8(0x1e,0xb9,0xf8,0xeb,0x0c,0x96,0x88,0x9b,0xf0,0x43,0xd1,0xb2,0x13,0x02,0x00,0x00)
};

// Debug helper: dump UUID bytes
static void log_uuid(const char *label, const uint8_t *v) {
    char b[49]; for(int i=0;i<16;i++) snprintf(b+i*3,4,"%02X ",v[i]);
    ESP_LOGI("BleAdapter","%s: %s",label,b);
}

// ─── Global instance ─────────────────────────────────────────────
BleAdapterImpl *BleAdapterImpl::inst_ = nullptr;

// Extract device name from BLE AD structure (len-type-value format)
// Type 0x09 = Complete Local Name, 0x08 = Shortened Local Name
static std::string parse_ble_name(const uint8_t *data, uint8_t len) {
    for (uint8_t i = 0; i + 1 < len; ) {
        uint8_t flen = data[i];
        if (flen == 0 || i + flen >= len) break;
        uint8_t type = data[i + 1];
        if ((type == 0x09 || type == 0x08) && flen >= 2) {
            return std::string((const char*)&data[i + 2], flen - 1);
        }
        i += flen + 1;
    }
    return std::string((const char*)data, len);
}

static int hexv(char c) {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
static bool parse_mac(const std::string &s, ble_addr_t &a) {
    a.type = BLE_ADDR_PUBLIC; int v[6]={},i=0;
    for (size_t j=0; j<s.size()&&i<6; j++) {
        int h=hexv(s[j]); if(h>=0&&j+1<s.size()){int l=hexv(s[j+1]); if(l>=0){v[i++]=(h<<4)|l;j++;continue;}}
        if(s[j]==':'||s[j]=='-')continue;
    }
    if (i!=6) return false;
    for (int k=0;k<6;k++) a.val[k]=v[5-k];
    return true;
}

std::string BleAdapterImpl::ble_name(const std::string &vin) {
    uint8_t h[20]; mbedtls_sha1((const uint8_t*)vin.c_str(), vin.size(), h);
    char b[17]; for (int i=0;i<8;i++) snprintf(b+i*2,3,"%02x",h[i]);
    return std::string("S")+b+"C";
}

// ─── Static callbacks ────────────────────────────────────────────
int BleAdapterImpl::_gap(struct ble_gap_event *e, void *) { return inst_->on_gap(e); }
int BleAdapterImpl::_disc_svc(uint16_t ch, const struct ble_gatt_error *err,
                               const struct ble_gatt_svc *svc, void *) {
    return inst_->on_disc_svc(ch, err, svc);
}
int BleAdapterImpl::_disc_chr(uint16_t ch, const struct ble_gatt_error *err,
                               const struct ble_gatt_chr *chr, void *) {
    return inst_->on_disc_chr(ch, err, chr);
}
int BleAdapterImpl::_disc_dsc(uint16_t ch, const struct ble_gatt_error *err,
                               uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *) {
    return inst_->on_disc_dsc(ch, err, chr_val_handle, dsc);
}

// ─── MTU exchange callback ──────────────────────────────────────
int BleAdapterImpl::_mtu_cb(uint16_t conn_handle, const struct ble_gatt_error *err,
                             uint16_t mtu, void *arg) {
    if (err->status == 0) {
        ESP_LOGI("BleAdapter", "MTU exchanged: conn=%u mtu=%u", conn_handle, mtu);
    } else {
        ESP_LOGW("BleAdapter", "MTU exchange failed: conn=%u status=%d mtu=%u",
                 conn_handle, err->status, mtu);
    }
    return 0;
}

// ─── Constructor / Destructor ────────────────────────────────────
BleAdapterImpl::BleAdapterImpl() { if(!inst_)inst_=this; nimble_port_init(); ble_hs_cfg.sync_cb=[](){ if(inst_){inst_->synced_=true;inst_->start_scan();}}; }
BleAdapterImpl::~BleAdapterImpl() { deinit(); if(inst_==this)inst_=nullptr; }

void BleAdapterImpl::init(const std::string &vin) {
    expected_name_ = ble_name(vin);
    ESP_LOGI(TAG,"VIN BLE name: %s", expected_name_.c_str());
    log_uuid("Expected Svc UUID", UUID_SVC.value);
    log_uuid("Expected Wr  UUID", UUID_WR.value);
    log_uuid("Expected Rd  UUID", UUID_RD.value);
    esp_nimble_hci_init();
    nimble_port_freertos_init([](void*){ nimble_port_run(); });
    nimble_init_ = true;
}

void BleAdapterImpl::deinit() {
    if (!nimble_init_) return;
    disconnect(); nimble_port_stop(); nimble_port_deinit();
    nimble_init_ = false;
}

// ─── TeslaBLE::BleAdapter ────────────────────────────────────────
void BleAdapterImpl::connect(const std::string &addr) {
    ble_addr_t a; if(!parse_mac(addr,a)){ESP_LOGE(TAG,"Bad mac: %s",addr.c_str());return;}
    do_connect(a);
}
void BleAdapterImpl::disconnect() {
    if (conn_handle_!=BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
    { std::lock_guard<std::mutex> lk(wq_mtx_); wq_={}; }
    rx_buf_.clear(); rx_expect_=0;
}
bool BleAdapterImpl::write(const std::vector<uint8_t> &data) {
    if (conn_handle_==BLE_HS_CONN_HANDLE_NONE||write_handle_==0) {
        ESP_LOGW(TAG, "write blocked: conn=%u w_handle=%u", conn_handle_, write_handle_);
        return false;
    }
    if (data.empty()) return false;
    // NOTE: Tesla library already prepends 2-byte length header.
    // We send data AS-IS without adding another header.
    ESP_LOGD(TAG, "write: %zu bytes", data.size());
    if (data.size() > 512) {
        ESP_LOGE(TAG, "write data too large: %zu bytes", data.size());
        return false;
    }
    try {
        std::lock_guard<std::mutex> lk(wq_mtx_);
        for (size_t o = 0; o < data.size(); o += BLK) {
            size_t n = data.size() - o;
            if (n > BLK) n = BLK;
            wq_.push(std::vector<uint8_t>(data.begin()+o, data.begin()+o+n));
        }
    } catch (const std::exception &e) {
        ESP_LOGE(TAG, "write exception: %s", e.what());
        return false;
    }
    return true;
}

// ─── Main loop ───────────────────────────────────────────────────
void BleAdapterImpl::process() {
    send_one();
    uint32_t now = xTaskGetTickCount();
    if (deferred_scan_at_ && now >= deferred_scan_at_) {
        deferred_scan_at_ = 0;
        if (state_ == State::IDLE) start_scan();
    }
    if (deferred_disc_at_ && now >= deferred_disc_at_) {
        deferred_disc_at_ = 0;
        if (state_ == State::DISCOVERING && conn_handle_ != BLE_HS_CONN_HANDLE_NONE) {
            start_disc();
        }
    }
}

// ─── GAP events ──────────────────────────────────────────────────
int BleAdapterImpl::on_gap(struct ble_gap_event *e) {
    switch (e->type) {
    case BLE_GAP_EVENT_DISC: {
        // Parse BLE advertisement data to extract the real device name
        std::string name = parse_ble_name(e->disc.data, e->disc.length_data);
        if (!name.empty()) {
            ESP_LOGD(TAG, "BLE dev: '%s' rssi=%d", name.c_str(), e->disc.rssi);
        }
        // Tesla BLE names: S<16hex>[CDRP], match by prefix
        bool match = false;
        if (name.size() >= 18 && name[0] == 'S') {
            std::string prefix = expected_name_.substr(0, 17);
            match = (name.compare(0, 17, prefix) == 0)
                 && (name[17] == 'C' || name[17] == 'D' || name[17] == 'R' || name[17] == 'P');
        }
        if (match) {
            ESP_LOGI(TAG, "*** MATCH! Connecting: %s rssi=%d ***", name.c_str(), e->disc.rssi);
            ble_gap_disc_cancel(); do_connect(e->disc.addr);
        } return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        if(!e->connect.status){
            conn_handle_=e->connect.conn_handle;
            ESP_LOGI(TAG,"Connected h=%u",conn_handle_);
            // Exchange MTU before service discovery (Tesla requires larger MTU)
            ble_att_set_preferred_mtu(256);
            int mtu_rc = ble_gattc_exchange_mtu(conn_handle_, _mtu_cb, nullptr);
            if (mtu_rc) ESP_LOGW(TAG, "MTU exchange req err=%d", mtu_rc);
            set_state(State::DISCOVERING);
            // Defer discovery by 1.5s to let MTU exchange + connection stabilize
            deferred_disc_at_ = xTaskGetTickCount() + pdMS_TO_TICKS(1500);
        } else { conn_handle_=BLE_HS_CONN_HANDLE_NONE; set_state(State::IDLE); deferred_scan_at_=xTaskGetTickCount()+pdMS_TO_TICKS(2000); }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT: {
        ESP_LOGI(TAG,"Disconnected r=%d",e->disconnect.reason);
        bool was=state_==State::CONNECTED;
        conn_handle_=BLE_HS_CONN_HANDLE_NONE;
        write_handle_=read_handle_=cccd_handle_=svc_start_=svc_end_=0;
        { std::lock_guard<std::mutex> lk(wq_mtx_); wq_={}; }
        rx_buf_.clear(); rx_expect_=0;
        set_state(State::IDLE);
        if(status_cb_)status_cb_(false);
        if(was){ ESP_LOGI(TAG,"Will re-scan"); deferred_scan_at_=xTaskGetTickCount()+pdMS_TO_TICKS(2000); }
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if(state_==State::SCANNING){ set_state(State::IDLE); deferred_scan_at_=xTaskGetTickCount()+pdMS_TO_TICKS(2000); }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        auto *o=e->notify_rx.om; uint16_t L=OS_MBUF_PKTLEN(o);
        std::vector<uint8_t> d(L); os_mbuf_copydata(o,0,L,d.data()); os_mbuf_free_chain(o);
        on_rx(d.data(),L); return 0;
    }
    default: return 0;
    }
}

// ─── Service discovery callback ──────────────────────────────────
int BleAdapterImpl::on_disc_svc(uint16_t ch, const struct ble_gatt_error *err,
                                 const struct ble_gatt_svc *svc) {
    if (err->status) {
        ESP_LOGI(TAG, "Svc disc done/err status=%d (0=OK, 524=EDONE)", err->status);
        // Discovery complete (or error)
        if (svc_start_) {
            ESP_LOGI(TAG,"Tesla service [%u-%u], discovering chars", svc_start_, svc_end_);
            disc_svcs_=false; chr_retries_=0;
            int rc=ble_gattc_disc_all_chrs(conn_handle_,svc_start_,svc_end_,_disc_chr,nullptr);
            if(rc){ESP_LOGE(TAG,"disc_all_chrs err=%d",rc);disconnect();}
        } else if (++svc_retries_<MAX_RETRY) {
            ESP_LOGW(TAG,"Svc retry %d/%d (status=%d)",svc_retries_,MAX_RETRY,err->status);
            vTaskDelay(pdMS_TO_TICKS(500));
            int rc=ble_gattc_disc_all_svcs(conn_handle_,_disc_svc,nullptr);
            if(rc){ESP_LOGE(TAG,"retry svc err=%d",rc);disconnect();}
        } else { ESP_LOGE(TAG,"Tesla service not found (gave up after %d retries)", MAX_RETRY); disconnect(); }
        return 0;
    }
    // Found a service
    if (svc) {
        if (svc->uuid.u.type == BLE_UUID_TYPE_16) {
            ESP_LOGI(TAG, "  Svc [%u-%u] UUID16=0x%04X", svc->start_handle, svc->end_handle, svc->uuid.u16.value);
        } else if (svc->uuid.u.type == BLE_UUID_TYPE_128) {
            log_uuid("  Svc UUID128", svc->uuid.u128.value);
            if (!memcmp(svc->uuid.u128.value, UUID_SVC.value, 16)) {
                svc_start_=svc->start_handle; svc_end_=svc->end_handle;
                ESP_LOGI(TAG,"  *** MATCH! Tesla service [%u-%u] ***",svc_start_,svc_end_);
            }
        } else if (svc->uuid.u.type == BLE_UUID_TYPE_32) {
            ESP_LOGI(TAG, "  Svc [%u-%u] UUID32=0x%08X", svc->start_handle, svc->end_handle, (unsigned)svc->uuid.u32.value);
        }
    }
    return 0;
}

// ─── CCCD descriptor discovery ───────────────────────────────────
static const ble_uuid16_t UUID_CCCD = BLE_UUID16_INIT(0x2902);

int BleAdapterImpl::on_disc_dsc(uint16_t ch, const struct ble_gatt_error *err,
                                 uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc) {
    if (err->status) {
        // Discovery complete (or error)
        if (cccd_handle_) {
            ESP_LOGI(TAG,"CCCD found at handle %u", cccd_handle_);
            enable_notif();
        } else {
            // Fallback: CCCD is typically val_handle + 1
            cccd_handle_ = read_handle_ + 1;
            ESP_LOGW(TAG,"CCCD not found via discovery, trying val_handle+1=%u", cccd_handle_);
            enable_notif();
        }
        return 0;
    }
    if (dsc && dsc->uuid.u.type == BLE_UUID_TYPE_16 && dsc->uuid.u16.value == 0x2902) {
        cccd_handle_ = dsc->handle;
        ESP_LOGI(TAG,"Found CCCD: handle=%u", dsc->handle);
    }
    return 0;
}

// ─── Characteristic discovery callback ───────────────────────────
int BleAdapterImpl::on_disc_chr(uint16_t ch, const struct ble_gatt_error *err,
                                 const struct ble_gatt_chr *chr) {
    if (err->status) {
        ESP_LOGI(TAG, "Chr disc done/err status=%d w=%u r=%u", err->status, write_handle_, read_handle_);
        if (write_handle_&&read_handle_) {
            ESP_LOGI(TAG,"Chars: w=%u r=%u",write_handle_,read_handle_);
            // Discover CCCD descriptor
            int rc=ble_gattc_disc_all_dscs(conn_handle_,read_handle_,read_handle_+5,
                                            _disc_dsc,nullptr);
            if(rc){ESP_LOGE(TAG,"disc_all_dscs err=%d",rc);disconnect();}
        } else if (++chr_retries_<MAX_RETRY) {
            ESP_LOGW(TAG,"Chr retry %d/%d (status=%d)",chr_retries_,MAX_RETRY,err->status);
            vTaskDelay(pdMS_TO_TICKS(500));
            int rc=ble_gattc_disc_all_chrs(conn_handle_,svc_start_,svc_end_,_disc_chr,nullptr);
            if(rc){ESP_LOGE(TAG,"retry chr err=%d",rc);disconnect();}
        } else { ESP_LOGE(TAG,"Chars not found (gave up after %d retries)", MAX_RETRY); disconnect(); }
        return 0;
    }
    if (chr && chr->uuid.u.type==BLE_UUID_TYPE_128) {
        if (!memcmp(chr->uuid.u128.value, UUID_WR.value, 16))
            write_handle_=chr->val_handle;
        else if (!memcmp(chr->uuid.u128.value, UUID_RD.value, 16)) {
            read_handle_=chr->val_handle;
            // Actual CCCD handle will be found via descriptor discovery
            cccd_handle_=0;
        }
    }
    return 0;
}

// ─── Scan / Connect ──────────────────────────────────────────────
void BleAdapterImpl::start_scan() {
    if (!synced_) return;
    set_state(State::SCANNING);
    struct ble_gap_disc_params p={};
    p.passive=0;             // active scan (Tesla name may be in scan response)
    p.filter_duplicates=0;   // don't filter - see everything
    p.window=80;             // 80*0.625=50ms window
    p.itvl=100;              // 100*0.625=62.5ms interval (80% duty cycle)
    p.filter_policy=0;
    p.limited=0;
    int rc=ble_gap_disc(BLE_OWN_ADDR_PUBLIC,0,&p,_gap,nullptr);
    if(rc){ESP_LOGE(TAG,"scan err=%d",rc);set_state(State::IDLE);}
    else ESP_LOGI(TAG,"Scanning for %s",expected_name_.c_str());
}
void BleAdapterImpl::do_connect(const ble_addr_t &addr) {
    set_state(State::CONNECTING);
    struct ble_gap_conn_params p={}; p.scan_itvl=p.scan_window=0x0010;
    p.itvl_min=p.itvl_max=0x0006; p.supervision_timeout=0x0BB8; // 30s to allow whitelist response
    int rc=ble_gap_connect(BLE_OWN_ADDR_PUBLIC,&addr,8000,&p,_gap,nullptr);
    if(rc){ESP_LOGE(TAG,"connect err=%d",rc);set_state(State::IDLE);start_scan();}
}

// ─── Discovery ───────────────────────────────────────────────────
void BleAdapterImpl::start_disc() {
    ESP_LOGI(TAG, "start_disc() called, conn_handle=%u", conn_handle_);
    disc_svcs_=true; svc_retries_=0; svc_start_=svc_end_=0;
    write_handle_=read_handle_=cccd_handle_=0;
    int rc=ble_gattc_disc_all_svcs(conn_handle_,_disc_svc,nullptr);
    if(rc){ESP_LOGE(TAG,"disc_all_svcs err=%d",rc);disconnect();}
}
int BleAdapterImpl::_cccd_cb(uint16_t, const struct ble_gatt_error*, struct ble_gatt_attr*, void*) {
    if (inst_) {
        ESP_LOGI(inst_->TAG, "Notifications enabled!");
        inst_->set_state(State::CONNECTED);
        if (inst_->status_cb_) inst_->status_cb_(true);
    }
    return 0;
}

void BleAdapterImpl::enable_notif() {
    uint8_t v[2]={1,0};
    int rc=ble_gattc_write_flat(conn_handle_,cccd_handle_,v,2,_cccd_cb,nullptr);
    if(rc){ESP_LOGE(TAG,"write_flat cccd err=%d",rc);disconnect();}
}

// ─── Write ───────────────────────────────────────────────────────
void BleAdapterImpl::send_one() {
    std::lock_guard<std::mutex> lk(wq_mtx_);
    if(wq_.empty()||write_handle_==0)return;
    auto &c=wq_.front();
    int rc=ble_gattc_write_no_rsp_flat(conn_handle_,write_handle_,c.data(),c.size());
    if(!rc) {
        wq_.pop();
    } else ESP_LOGW(TAG,"write err=%d",rc);
}

// ─── RX ──────────────────────────────────────────────────────────
// BLE notifications from Tesla vehicle are complete protobuf messages
// without a 2-byte length prefix. Pass directly to the Tesla library.
void BleAdapterImpl::on_rx(const uint8_t *d, size_t n) {
    std::vector<uint8_t> msg(d, d+n);
    if(data_cb_)data_cb_(msg);
}

void BleAdapterImpl::set_state(State s) {
    if(s!=state_){
        const char *ns[]={"IDLE","SCAN","CONNECTING","DISCOVER","CONNECTED"};
        ESP_LOGI(TAG,"%s -> %s",ns[(int)state_],ns[(int)s]); state_=s;
    }
}
