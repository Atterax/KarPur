// KarPur: SDK calls of Scarlet/Violet that reach online, wireless and system-applet services TotK never uses.
//
// The game runs offline: no Internet (nifm, socket, ssl, nsd, bcat, web, ec), no Bluetooth LE (Pokemon GO Plus +
// pairing), no local wireless (ldn), no screenshots into the album, no profanity-filter service. Each call answers the
// way the SDK does when that service is unavailable, so the game takes its own offline path. The first call of each
// is logged with its caller, which shows what the game actually reaches.
//
// AudioOut (audout:u) is here too: Scarlet/Violet open the default audio output besides the audio renderer TotK uses.
// It plays nothing yet; buffers come back as the host clock passes their length, so a game that waits for released
// buffers keeps its timing.
#include <stdint.h>
#include <string.h>

#include "host.h"
#include "platform/platform.h"

#define KP_RESULT(module, desc) ((uint32_t)(module) | ((uint32_t)(desc) << 9))
// Result codes: the ones the SDK's clients return when the service is unreachable; the game only tests != 0.
#define R_NIFM_NO_NETWORK KP_RESULT(110, 300)
#define R_BCAT_UNAVAILABLE KP_RESULT(122, 1)
#define R_SSL_UNAVAILABLE KP_RESULT(123, 1)
#define R_NSD_UNAVAILABLE KP_RESULT(141, 1)
#define R_BLUETOOTH_UNAVAILABLE KP_RESULT(113, 1)
#define R_LDN_UNAVAILABLE KP_RESULT(203, 32)
#define R_ALBUM_UNAVAILABLE KP_RESULT(22, 1)
#define R_WEB_CANCELLED KP_RESULT(166, 1)
#define SOCKET_ENETDOWN 50      // BSD errno, as nn::socket reports it

static const char *caller(Cpu *c, char *buf, int n) { return ldr_describe(c->x[30] - 4, buf, n); }

// One flag per stub, so each logs its first call only.
static void first_call(volatile int32_t *seen, const char *what, Cpu *c) {
    if (plat_cas32(seen, 1, 0)) return;   // (pointer, new value, expected): 0 -> 1, returns the old value
    char lr[128];
    rt_log("offline: %s (first call, from %s)", what, caller(c, lr, sizeof lr));
}

#define STUB(fn, sym, what, body)                                  \
    static void fn(Cpu *c) {                                       \
        static volatile int32_t seen;                              \
        first_call(&seen, what, c);                                \
        body;                                                      \
    }                                                              \
    HOST_OVERRIDE(sym, fn)

#define RET(v) ret_x(c, (uint64_t)(v))
#define NOTHING (void)c
// bool *out at argument i set to false, then Result 0
#define OUT_FALSE(i) do { uint8_t *o_ = arg_p(c, i); if (o_) *o_ = 0; ret_x(c, 0); } while (0)

// ---- nifm: no network connection
STUB(nifm_init, "_ZN2nn4nifm10InitializeEv", "nifm::Initialize", RET(0));
STUB(nifm_available, "_ZN2nn4nifm18IsNetworkAvailableEv", "nifm::IsNetworkAvailable -> false", RET(0));
STUB(nifm_local_mode, "_ZN2nn4nifm19SetLocalNetworkModeEb", "nifm::SetLocalNetworkMode", NOTHING);
STUB(nifm_cancel, "_ZN2nn4nifm20CancelNetworkRequestEv", "nifm::CancelNetworkRequest", NOTHING);
STUB(nifm_ip, "_ZN2nn4nifm22GetCurrentIpConfigInfoEP7in_addrS2_S2_S2_S2_", "nifm::GetCurrentIpConfigInfo", RET(R_NIFM_NO_NETWORK));
STUB(nifm_proxy, "_ZN2nn4nifm22GetCurrentProxySettingEPNS0_12ProxySettingE", "nifm::GetCurrentProxySetting", RET(R_NIFM_NO_NETWORK));
STUB(nifm_submit, "_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv", "nifm::SubmitNetworkRequestAndWait", NOTHING);
STUB(nifm_handle, "_ZN2nn4nifm31HandleNetworkRequestErrorResultEv", "nifm::HandleNetworkRequestErrorResult", RET(R_NIFM_NO_NETWORK));

// ---- socket: the library starts, but no socket opens. Address helpers (InetNtop, htons, ...) stay the SDK's own.
static void sock_fail(Cpu *c) { ret_x(c, (uint64_t)(int64_t)-1); }
STUB(sock_init, "_ZN2nn6socket10InitializeERKNS0_6ConfigE", "socket::Initialize", RET(0));
STUB(sock_socket, "_ZN2nn6socket6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE", "socket::Socket", sock_fail(c));
STUB(sock_socket2, "_ZN2nn6socket6SocketEiii", "socket::Socket", sock_fail(c));
STUB(sock_setopt, "_ZN2nn6socket10SetSockOptEiNS0_5LevelENS0_6OptionEPKvj", "socket::SetSockOpt", sock_fail(c));
STUB(sock_bind, "_ZN2nn6socket4BindEiPK8sockaddrj", "socket::Bind", sock_fail(c));
STUB(sock_bind2, "_ZN2nn6socket4BindEiPKNS0_8SockAddrEj", "socket::Bind", sock_fail(c));
STUB(sock_poll, "_ZN2nn6socket4PollEP6pollfdmi", "socket::Poll", sock_fail(c));
STUB(sock_close, "_ZN2nn6socket5CloseEi", "socket::Close", RET(0));
STUB(sock_sendto, "_ZN2nn6socket6SendToEiPKvmNS0_7MsgFlagEPKNS0_8SockAddrEj", "socket::SendTo", sock_fail(c));
STUB(sock_sendto2, "_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj", "socket::SendTo", sock_fail(c));
STUB(sock_recvfrom, "_ZN2nn6socket8RecvFromEiPvmNS0_7MsgFlagEPNS0_8SockAddrEPj", "socket::RecvFrom", sock_fail(c));
STUB(sock_recvfrom2, "_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj", "socket::RecvFrom", sock_fail(c));
STUB(sock_shutdown, "_ZN2nn6socket8ShutdownEii", "socket::Shutdown", sock_fail(c));
STUB(sock_eventfd, "_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE", "socket::EventFd", sock_fail(c));
STUB(sock_efd_read, "_ZN2nn6socket11EventFdReadEiPm", "socket::EventFdRead", sock_fail(c));
STUB(sock_efd_write, "_ZN2nn6socket12EventFdWriteEim", "socket::EventFdWrite", sock_fail(c));
STUB(sock_errno, "_ZN2nn6socket12GetLastErrnoEv", "socket::GetLastErrno", RET(SOCKET_ENETDOWN));
STUB(sock_error, "_ZN2nn6socket12GetLastErrorEv", "socket::GetLastError", RET(SOCKET_ENETDOWN));
// getaddrinfo: EAI_FAIL (4); FreeAddrInfo has nothing to free
STUB(sock_gai, "_ZN2nn6socket11GetAddrInfoEPKcS2_PK8addrinfoPPS3_", "socket::GetAddrInfo", RET(4));
STUB(sock_gai2, "_ZN2nn6socket11GetAddrInfoEPKcS2_PKNS0_8AddrInfoEPPS3_", "socket::GetAddrInfo", RET(4));
STUB(sock_freeai, "_ZN2nn6socket12FreeAddrInfoEP8addrinfo", "socket::FreeAddrInfo", NOTHING);
STUB(sock_freeai2, "_ZN2nn6socket12FreeAddrInfoEPNS0_8AddrInfoE", "socket::FreeAddrInfo", NOTHING);
static const char gai_text[] = "no network (offline)";
STUB(sock_gaistr, "_ZN2nn6socket11GAIStrErrorENS0_7AiErrnoE", "socket::GAIStrError", RET((uintptr_t)gai_text));

// ---- ssl
STUB(ssl_init, "_ZN2nn3ssl10InitializeEv", "ssl::Initialize", RET(0));
STUB(ssl_ctx_new, "_ZN2nn3ssl7ContextC1Ev", "ssl::Context::Context", NOTHING);
STUB(ssl_ctx_del, "_ZN2nn3ssl7ContextD1Ev", "ssl::Context::~Context", NOTHING);
STUB(ssl_ctx_create, "_ZN2nn3ssl7Context6CreateENS1_10SslVersionE", "ssl::Context::Create", RET(R_SSL_UNAVAILABLE));
STUB(ssl_ctx_destroy, "_ZN2nn3ssl7Context7DestroyEv", "ssl::Context::Destroy", RET(0));
STUB(ssl_cert_size, "_ZN2nn3ssl14BuiltInManager28GetBuiltInCertificateBufSizeEPjPNS0_15CaCertificateIdEj",
     "ssl::GetBuiltInCertificateBufSize", RET(R_SSL_UNAVAILABLE));
STUB(ssl_certs, "_ZN2nn3ssl14BuiltInManager22GetBuiltInCertificatesEPPNS0_26BuiltInCertificateInfoBaseEPhjPNS0_15CaCertificateIdEj",
     "ssl::GetBuiltInCertificates", RET(R_SSL_UNAVAILABLE));

// ---- nsd: the environment identifier is the retail one ("lp1"); nothing resolves
STUB(nsd_env, "_ZN2nn3nsd24GetEnvironmentIdentifierEPNS0_21EnvironmentIdentifierE", "nsd::GetEnvironmentIdentifier",
     do { char *o_ = arg_p(c, 0); if (o_) { memset(o_, 0, 8); memcpy(o_, "lp1", 3); } ret_x(c, 0); } while (0));
STUB(nsd_resolve, "_ZN2nn3nsd9ResolveExEPNS0_4FqdnERKS1_", "nsd::ResolveEx", RET(R_NSD_UNAVAILABLE));

// ---- bcat: no delivery cache (news and event data from the Internet)
STUB(bcat_init, "_ZN2nn4bcat10InitializeEv", "bcat::Initialize", NOTHING);
STUB(bcat_mount, "_ZN2nn4bcat25MountDeliveryCacheStorageEv", "bcat::MountDeliveryCacheStorage", RET(R_BCAT_UNAVAILABLE));
STUB(bcat_unmount, "_ZN2nn4bcat27UnmountDeliveryCacheStorageEv", "bcat::UnmountDeliveryCacheStorage", NOTHING);
STUB(bcat_enum, "_ZN2nn4bcat31EnumerateDeliveryCacheDirectoryEPiPNS0_13DirectoryNameEi", "bcat::EnumerateDeliveryCacheDirectory",
     RET(R_BCAT_UNAVAILABLE));
STUB(bcat_sync, "_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressE", "bcat::RequestSyncDeliveryCache",
     RET(R_BCAT_UNAVAILABLE));
STUB(bcat_file_new, "_ZN2nn4bcat17DeliveryCacheFileC1Ev", "bcat::DeliveryCacheFile::DeliveryCacheFile", NOTHING);
STUB(bcat_file_del, "_ZN2nn4bcat17DeliveryCacheFileD1Ev", "bcat::DeliveryCacheFile::~DeliveryCacheFile", NOTHING);
STUB(bcat_file_open, "_ZN2nn4bcat17DeliveryCacheFile4OpenEPKc", "bcat::DeliveryCacheFile::Open", RET(R_BCAT_UNAVAILABLE));
STUB(bcat_file_read, "_ZN2nn4bcat17DeliveryCacheFile4ReadEPmlPvm", "bcat::DeliveryCacheFile::Read", RET(R_BCAT_UNAVAILABLE));
STUB(bcat_file_close, "_ZN2nn4bcat17DeliveryCacheFile5CloseEv", "bcat::DeliveryCacheFile::Close", NOTHING);
STUB(bcat_file_size, "_ZN2nn4bcat17DeliveryCacheFile7GetSizeEv", "bcat::DeliveryCacheFile::GetSize", RET(0));
STUB(bcat_dir_new, "_ZN2nn4bcat22DeliveryCacheDirectoryC1Ev", "bcat::DeliveryCacheDirectory::DeliveryCacheDirectory", NOTHING);
STUB(bcat_dir_del, "_ZN2nn4bcat22DeliveryCacheDirectoryD1Ev", "bcat::DeliveryCacheDirectory::~DeliveryCacheDirectory", NOTHING);
STUB(bcat_dir_open, "_ZN2nn4bcat22DeliveryCacheDirectory4OpenEPKc", "bcat::DeliveryCacheDirectory::Open", RET(R_BCAT_UNAVAILABLE));
STUB(bcat_dir_open2, "_ZN2nn4bcat22DeliveryCacheDirectory4OpenERKNS0_13DirectoryNameE", "bcat::DeliveryCacheDirectory::Open",
     RET(R_BCAT_UNAVAILABLE));
STUB(bcat_dir_read, "_ZN2nn4bcat22DeliveryCacheDirectory4ReadEPiPNS0_27DeliveryCacheDirectoryEntryEi",
     "bcat::DeliveryCacheDirectory::Read", RET(R_BCAT_UNAVAILABLE));
STUB(bcat_dir_close, "_ZN2nn4bcat22DeliveryCacheDirectory5CloseEv", "bcat::DeliveryCacheDirectory::Close", NOTHING);
STUB(bcat_prog_new, "_ZN2nn4bcat21DeliveryCacheProgressC1Ev", "bcat::DeliveryCacheProgress::DeliveryCacheProgress", NOTHING);
STUB(bcat_prog_del, "_ZN2nn4bcat21DeliveryCacheProgressD1Ev", "bcat::DeliveryCacheProgress::~DeliveryCacheProgress", NOTHING);
STUB(bcat_prog_update, "_ZN2nn4bcat21DeliveryCacheProgress6UpdateEv", "bcat::DeliveryCacheProgress::Update", NOTHING);
STUB(bcat_prog_status, "_ZNK2nn4bcat21DeliveryCacheProgress9GetStatusEv", "bcat::DeliveryCacheProgress::GetStatus", RET(9));   // Done
STUB(bcat_prog_result, "_ZNK2nn4bcat21DeliveryCacheProgress9GetResultEv", "bcat::DeliveryCacheProgress::GetResult",
     RET(R_BCAT_UNAVAILABLE));

// ---- web, eShop
STUB(web_show, "_ZN2nn3web11ShowWebPageEPNS0_18WebPageReturnValueERKNS0_14ShowWebPageArgE", "web::ShowWebPage", RET(R_WEB_CANCELLED));
STUB(ec_shop, "_ZN2nn2ec24ShowShopAddOnContentListERKNS_7account10UserHandleE", "ec::ShowShopAddOnContentList", NOTHING);

// ---- album: no screenshots
STUB(album_init, "_ZN2nn5album10InitializeEv", "album::Initialize", NOTHING);
STUB(album_fini, "_ZN2nn5album8FinalizeEv", "album::Finalize", NOTHING);
STUB(album_save, "_ZN2nn5album21SaveCurrentScreenshotENS0_17AlbumReportOptionE", "album::SaveCurrentScreenshot",
     RET(R_ALBUM_UNAVAILABLE));

// ---- ngc: the profanity filter starts; its checks are not imported
STUB(ngc_new, "_ZN2nn3ngc15ProfanityFilterC1Ev", "ngc::ProfanityFilter::ProfanityFilter", NOTHING);
STUB(ngc_init, "_ZN2nn3ngc15ProfanityFilter10InitializeEPvm", "ngc::ProfanityFilter::Initialize", RET(0));
STUB(ngc_fini, "_ZN2nn3ngc15ProfanityFilter8FinalizeEv", "ngc::ProfanityFilter::Finalize", NOTHING);

// ---- vi: the window outlives the game's display objects
STUB(vi_close, "_ZN2nn2vi12CloseDisplayEPNS0_7DisplayE", "vi::CloseDisplay", NOTHING);
STUB(vi_destroy, "_ZN2nn2vi12DestroyLayerEPNS0_5LayerE", "vi::DestroyLayer", NOTHING);
STUB(vi_fini, "_ZN2nn2vi8FinalizeEv", "vi::Finalize", NOTHING);

// ---- ldn: no local wireless (Union Circle and trades nearby)
STUB(ldn_init, "_ZN2nn3ldn10InitializeEv", "ldn::Initialize", RET(R_LDN_UNAVAILABLE));
STUB(ldn_fini, "_ZN2nn3ldn8FinalizeEv", "ldn::Finalize", NOTHING);
STUB(ldn_state, "_ZN2nn3ldn8GetStateEv", "ldn::GetState", RET(0));   // None
STUB(ldn_reason, "_ZN2nn3ldn19GetDisconnectReasonEv", "ldn::GetDisconnectReason", RET(0));
STUB(ldn_attach, "_ZN2nn3ldn22AttachStateChangeEventEPNS_2os15SystemEventTypeE", "ldn::AttachStateChangeEvent",
     do { uint64_t ev_ = arg_x(c, 0); if (ev_) kern_bind_system_event(c, ev_, 0); } while (0));
#define LDN_FAIL(fn, sym, what) STUB(fn, sym, what, RET(R_LDN_UNAVAILABLE))
LDN_FAIL(ldn_disconnect, "_ZN2nn3ldn10DisconnectEv", "ldn::Disconnect");
LDN_FAIL(ldn_open_station, "_ZN2nn3ldn11OpenStationEv", "ldn::OpenStation");
LDN_FAIL(ldn_close_station, "_ZN2nn3ldn12CloseStationEv", "ldn::CloseStation");
LDN_FAIL(ldn_open_ap, "_ZN2nn3ldn15OpenAccessPointEv", "ldn::OpenAccessPoint");
LDN_FAIL(ldn_close_ap, "_ZN2nn3ldn16CloseAccessPointEv", "ldn::CloseAccessPoint");
LDN_FAIL(ldn_scan, "_ZN2nn3ldn4ScanEPNS0_11NetworkInfoEPiiRKNS0_10ScanFilterEi", "ldn::Scan");
LDN_FAIL(ldn_scan_private, "_ZN2nn3ldn11ScanPrivateEPNS0_11NetworkInfoEPiiRKNS0_10ScanFilterEi", "ldn::ScanPrivate");
LDN_FAIL(ldn_create, "_ZN2nn3ldn13CreateNetworkERKNS0_13NetworkConfigERKNS0_14SecurityConfigERKNS0_10UserConfigE", "ldn::CreateNetwork");
LDN_FAIL(ldn_create_private, "_ZN2nn3ldn20CreateNetworkPrivateERKNS0_13NetworkConfigERKNS0_14SecurityConfigERKNS0_17SecurityParameterERKNS0_10UserConfigEiPKNS0_12AddressEntryE",
         "ldn::CreateNetworkPrivate");
LDN_FAIL(ldn_destroy, "_ZN2nn3ldn14DestroyNetworkEv", "ldn::DestroyNetwork");
LDN_FAIL(ldn_connect, "_ZN2nn3ldn7ConnectERKNS0_11NetworkInfoERKNS0_14SecurityConfigERKNS0_10UserConfigEiNS0_13ConnectOptionE", "ldn::Connect");
LDN_FAIL(ldn_connect_private, "_ZN2nn3ldn14ConnectPrivateERKNS0_13NetworkConfigERKNS0_14SecurityConfigERKNS0_17SecurityParameterERKNS0_10UserConfigEiNS0_13ConnectOptionE",
         "ldn::ConnectPrivate");
LDN_FAIL(ldn_ip, "_ZN2nn3ldn14GetIpv4AddressEPNS0_11Ipv4AddressEPNS0_10SubnetMaskE", "ldn::GetIpv4Address");
LDN_FAIL(ldn_info, "_ZN2nn3ldn14GetNetworkInfoEPNS0_11NetworkInfoE", "ldn::GetNetworkInfo");
LDN_FAIL(ldn_advertise, "_ZN2nn3ldn16SetAdvertiseDataEPKvm", "ldn::SetAdvertiseData");
LDN_FAIL(ldn_filter, "_ZN2nn3ldn20AddAcceptFilterEntryENS0_10MacAddressE", "ldn::AddAcceptFilterEntry");
LDN_FAIL(ldn_security, "_ZN2nn3ldn20GetSecurityParameterEPNS0_17SecurityParameterE", "ldn::GetSecurityParameter");
LDN_FAIL(ldn_policy, "_ZN2nn3ldn22SetStationAcceptPolicyENS0_12AcceptPolicyE", "ldn::SetStationAcceptPolicy");
LDN_FAIL(ldn_reject, "_ZN2nn3ldn6RejectENS0_11Ipv4AddressE", "ldn::Reject");

// ---- bluetooth LE (accessory pairing): unavailable; events exist but never fire
STUB(bt_init, "_ZN2nn9bluetooth13InitializeBleEv", "bluetooth::InitializeBle", RET(R_BLUETOOTH_UNAVAILABLE));
#define BT_EVENT(fn, sym, what) \
    STUB(fn, sym, what, do { uint64_t ev_ = arg_x(c, 0); if (ev_) kern_bind_system_event(c, ev_, 0); ret_x(c, 0); } while (0))
BT_EVENT(bt_scan_event, "_ZN2nn9bluetooth19AcquireBleScanEventEPNS_2os15SystemEventTypeE", "bluetooth::AcquireBleScanEvent");
BT_EVENT(bt_mtu_event, "_ZN2nn9bluetooth24AcquireBleMtuConfigEventEPNS_2os15SystemEventTypeE", "bluetooth::AcquireBleMtuConfigEvent");
BT_EVENT(bt_gatt_event, "_ZN2nn9bluetooth28AcquireBleGattOperationEventEPNS_2os15SystemEventTypeE", "bluetooth::AcquireBleGattOperationEvent");
BT_EVENT(bt_disc_event, "_ZN2nn9bluetooth31AcquireBleServiceDiscoveryEventEPNS_2os15SystemEventTypeE",
         "bluetooth::AcquireBleServiceDiscoveryEvent");
BT_EVENT(bt_conn_event, "_ZN2nn9bluetooth37AcquireBleConnectionStateChangedEventEPNS_2os15SystemEventTypeE",
         "bluetooth::AcquireBleConnectionStateChangedEvent");
#define BT_FAIL(fn, sym, what) STUB(fn, sym, what, RET(R_BLUETOOTH_UNAVAILABLE))
#define BT_NOTHING(fn, sym, what) STUB(fn, sym, what, NOTHING)
#define BT_ZERO(fn, sym, what) STUB(fn, sym, what, RET(0))
BT_NOTHING(bt_svc_new, "_ZN2nn9bluetooth11GattServiceC1Ev", "bluetooth::GattService::GattService");
BT_NOTHING(bt_svc_del, "_ZN2nn9bluetooth11GattServiceD1Ev", "bluetooth::GattService::~GattService");
BT_NOTHING(bt_desc_new, "_ZN2nn9bluetooth14GattDescriptorC1Ev", "bluetooth::GattDescriptor::GattDescriptor");
BT_NOTHING(bt_desc_del, "_ZN2nn9bluetooth14GattDescriptorD1Ev", "bluetooth::GattDescriptor::~GattDescriptor");
BT_NOTHING(bt_char_new, "_ZN2nn9bluetooth18GattCharacteristicC1Ev", "bluetooth::GattCharacteristic::GattCharacteristic");
BT_NOTHING(bt_char_del, "_ZN2nn9bluetooth18GattCharacteristicD1Ev", "bluetooth::GattCharacteristic::~GattCharacteristic");
BT_NOTHING(bt_desc_set, "_ZN2nn9bluetooth14GattDescriptor8SetValueEPKvm", "bluetooth::GattDescriptor::SetValue");
BT_NOTHING(bt_char_set, "_ZN2nn9bluetooth18GattCharacteristic8SetValueEPKvm", "bluetooth::GattCharacteristic::SetValue");
BT_FAIL(bt_get_service, "_ZN2nn9bluetooth14GetGattServiceEPNS0_11GattServiceERKNS0_17GattAttributeUuidEj", "bluetooth::GetGattService");
BT_FAIL(bt_mtu, "_ZN2nn9bluetooth15ConfigureBleMtuEjt", "bluetooth::ConfigureBleMtu");
BT_ZERO(bt_scan_result, "_ZN2nn9bluetooth16GetBleScanResultEPNS0_13BleScanResultEi", "bluetooth::GetBleScanResult");
BT_FAIL(bt_connect, "_ZN2nn9bluetooth19ConnectToGattServerERKNS0_7AddressE", "bluetooth::ConnectToGattServer");
BT_ZERO(bt_scan_param, "_ZN2nn9bluetooth19GetBleScanParameterEPNS0_17GattAttributeUuidEi", "bluetooth::GetBleScanParameter");
BT_FAIL(bt_write_desc, "_ZN2nn9bluetooth19WriteGattDescriptorERKNS0_14GattDescriptorE", "bluetooth::WriteGattDescriptor");
BT_FAIL(bt_op_result, "_ZN2nn9bluetooth22GetGattOperationResultEPNS0_26BleClientGattOperationInfoE", "bluetooth::GetGattOperationResult");
BT_FAIL(bt_read_char, "_ZN2nn9bluetooth22ReadGattCharacteristicERKNS0_18GattCharacteristicE", "bluetooth::ReadGattCharacteristic");
BT_FAIL(bt_stop_scan, "_ZN2nn9bluetooth22StopBleScanSmartDeviceEv", "bluetooth::StopBleScanSmartDevice");
BT_FAIL(bt_start_scan, "_ZN2nn9bluetooth23StartBleScanSmartDeviceERKNS0_17GattAttributeUuidE", "bluetooth::StartBleScanSmartDevice");
BT_FAIL(bt_write_char, "_ZN2nn9bluetooth23WriteGattCharacteristicERKNS0_18GattCharacteristicE", "bluetooth::WriteGattCharacteristic");
BT_FAIL(bt_disconnect, "_ZN2nn9bluetooth24DisconnectFromGattServerEj", "bluetooth::DisconnectFromGattServer");
BT_ZERO(bt_conn_list, "_ZN2nn9bluetooth24GetBleConnectionInfoListEPNS0_17BleConnectionInfoEi", "bluetooth::GetBleConnectionInfoList");
BT_FAIL(bt_reg_notify, "_ZN2nn9bluetooth33RegisterGattOperationNotificationERKNS0_17GattAttributeUuidE",
        "bluetooth::RegisterGattOperationNotification");
BT_FAIL(bt_unreg_notify, "_ZN2nn9bluetooth35UnregisterGattOperationNotificationERKNS0_17GattAttributeUuidE",
        "bluetooth::UnregisterGattOperationNotification");
BT_FAIL(bt_enable_notify, "_ZN2nn9bluetooth36EnableGattCharacteristicNotificationERKNS0_18GattCharacteristicEb",
        "bluetooth::EnableGattCharacteristicNotification");
BT_ZERO(bt_chars, "_ZNK2nn9bluetooth11GattService18GetCharacteristicsEPNS0_18GattCharacteristicEi", "bluetooth::GattService::GetCharacteristics");
BT_ZERO(bt_uuid, "_ZNK2nn9bluetooth13GattAttribute7GetUuidEv", "bluetooth::GattAttribute::GetUuid");
BT_ZERO(bt_desc_char, "_ZNK2nn9bluetooth14GattDescriptor17GetCharacteristicEPNS0_18GattCharacteristicE",
        "bluetooth::GattDescriptor::GetCharacteristic");
BT_ZERO(bt_char_descs, "_ZNK2nn9bluetooth18GattCharacteristic14GetDescriptorsEPNS0_14GattDescriptorEi",
        "bluetooth::GattCharacteristic::GetDescriptors");

// ---- AudioOut (audout:u): a silent output that releases each buffer once its length has passed on the host clock.
// AudioOutBuffer accessors (SetAudioOutBufferInfo, GetAudioOutBufferDataSize, ...) stay the SDK's own; the data size
// is read through the SDK's GetAudioOutBufferDataSize so this file needs no knowledge of the struct.
#define AO_RATE 48000
#define AO_CHANNELS 2
#define AO_MAX_OUTS 4
#define AO_MAX_BUFS 64
typedef struct {
    uint64_t out;                       // the guest AudioOut, 0 when the slot is free
    int started;
    uint64_t bufs[AO_MAX_BUFS];         // appended, not yet released, oldest first
    uint64_t due_ns[AO_MAX_BUFS];       // when each is played out
    uint64_t samples[AO_MAX_BUFS];      // frames in each
    int n;
    uint64_t end_ns;                    // when the last appended buffer finishes
    uint64_t played;                    // frames released so far
} AudioOutSlot;
static AudioOutSlot ao[AO_MAX_OUTS];
static PlatMutex ao_lock = PLAT_MUTEX_INIT;

static AudioOutSlot *ao_slot(uint64_t out, int create) {
    for (int i = 0; i < AO_MAX_OUTS; i++)
        if (ao[i].out == out) return &ao[i];
    if (!create) return NULL;
    for (int i = 0; i < AO_MAX_OUTS; i++)
        if (!ao[i].out) {
            memset(&ao[i], 0, sizeof ao[i]);
            ao[i].out = out;
            return &ao[i];
        }
    rt_fatal("audout: more than %d audio outputs open", AO_MAX_OUTS);
}

// Result OpenDefaultAudioOut(AudioOut *, const AudioOutParameter &)
static void ao_open(Cpu *c) {
    plat_mutex_lock(&ao_lock);
    ao_slot(arg_x(c, 0), 1);
    plat_mutex_unlock(&ao_lock);
    rt_log("audout: default audio output opened (%d Hz, %d channels, silent)", AO_RATE, AO_CHANNELS);
    ret_x(c, 0);
}
static void ao_close(Cpu *c) {
    plat_mutex_lock(&ao_lock);
    AudioOutSlot *s = ao_slot(arg_x(c, 0), 0);
    if (s) s->out = 0;
    plat_mutex_unlock(&ao_lock);
}
static void ao_start(Cpu *c) {
    plat_mutex_lock(&ao_lock);
    AudioOutSlot *s = ao_slot(arg_x(c, 0), 1);
    s->started = 1;
    s->end_ns = kern_now_ns();
    plat_mutex_unlock(&ao_lock);
    ret_x(c, 0);
}
static void ao_stop(Cpu *c) {
    plat_mutex_lock(&ao_lock);
    AudioOutSlot *s = ao_slot(arg_x(c, 0), 0);
    if (s) s->started = 0;
    plat_mutex_unlock(&ao_lock);
}
// AudioOutState GetAudioOutState(const AudioOut *): Started 0, Stopped 1
static void ao_state(Cpu *c) {
    plat_mutex_lock(&ao_lock);
    AudioOutSlot *s = ao_slot(arg_x(c, 0), 0);
    int started = s && s->started;
    plat_mutex_unlock(&ao_lock);
    ret_x(c, started ? 0 : 1);
}
static void ao_rate(Cpu *c) { (void)c; ret_x(c, AO_RATE); }
static void ao_channels(Cpu *c) { (void)c; ret_x(c, AO_CHANNELS); }
static void ao_format(Cpu *c) { (void)c; ret_x(c, 2); }   // SampleFormat_PcmInt16
static void ao_played(Cpu *c) {
    plat_mutex_lock(&ao_lock);
    AudioOutSlot *s = ao_slot(arg_x(c, 0), 0);
    uint64_t v = s ? s->played : 0;
    plat_mutex_unlock(&ao_lock);
    ret_x(c, v);
}
// bool AppendAudioOutBuffer(AudioOut *, AudioOutBuffer *)
static void ao_append(Cpu *c) {
    static uint64_t data_size_fn;
    if (!data_size_fn) data_size_fn = ldr_symbol("_ZN2nn5audio25GetAudioOutBufferDataSizeEPKNS0_14AudioOutBufferE");
    uint64_t out = arg_x(c, 0), buf = arg_x(c, 1);
    uint64_t bytes = data_size_fn && buf ? rt_call(c, data_size_fn, 1, buf) : 0;
    uint64_t frames = bytes / (2 * AO_CHANNELS);
    plat_mutex_lock(&ao_lock);
    AudioOutSlot *s = ao_slot(out, 1);
    int ok = buf && s->n < AO_MAX_BUFS;
    if (ok) {
        uint64_t now = kern_now_ns(), start = s->end_ns > now ? s->end_ns : now;
        s->end_ns = start + frames * 1000000000ull / AO_RATE;
        s->bufs[s->n] = buf;
        s->due_ns[s->n] = s->end_ns;
        s->samples[s->n] = frames;
        s->n++;
    }
    plat_mutex_unlock(&ao_lock);
    ret_x(c, (uint64_t)ok);
}
// AudioOutBuffer *GetReleasedAudioOutBuffer(AudioOut *): the oldest buffer whose time has passed, else null
static void ao_released(Cpu *c) {
    uint64_t r = 0;
    plat_mutex_lock(&ao_lock);
    AudioOutSlot *s = ao_slot(arg_x(c, 0), 0);
    if (s && s->n && s->due_ns[0] <= kern_now_ns()) {
        r = s->bufs[0];
        s->played += s->samples[0];
        memmove(s->bufs, s->bufs + 1, (size_t)(s->n - 1) * sizeof *s->bufs);
        memmove(s->due_ns, s->due_ns + 1, (size_t)(s->n - 1) * sizeof *s->due_ns);
        memmove(s->samples, s->samples + 1, (size_t)(s->n - 1) * sizeof *s->samples);
        s->n--;
    }
    plat_mutex_unlock(&ao_lock);
    ret_x(c, r);
}

HOST_OVERRIDE("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE", ao_open);
HOST_OVERRIDE("_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE", ao_close);
HOST_OVERRIDE("_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE", ao_start);
HOST_OVERRIDE("_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE", ao_stop);
HOST_OVERRIDE("_ZN2nn5audio16GetAudioOutStateEPKNS0_8AudioOutE", ao_state);
HOST_OVERRIDE("_ZN2nn5audio21GetAudioOutSampleRateEPKNS0_8AudioOutE", ao_rate);
HOST_OVERRIDE("_ZN2nn5audio23GetAudioOutChannelCountEPKNS0_8AudioOutE", ao_channels);
HOST_OVERRIDE("_ZN2nn5audio23GetAudioOutSampleFormatEPKNS0_8AudioOutE", ao_format);
HOST_OVERRIDE("_ZN2nn5audio28GetAudioOutPlayedSampleCountEPKNS0_8AudioOutE", ao_played);
HOST_OVERRIDE("_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE", ao_append);
HOST_OVERRIDE("_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE", ao_released);
