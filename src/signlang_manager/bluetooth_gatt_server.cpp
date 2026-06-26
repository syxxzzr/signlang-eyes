#include "bluetooth_gatt_server.hpp"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>
#include <gio/gio.h>
#include <glib.h>
#include <stdexcept>

namespace signlang::signlang_manager {
  namespace {

    constexpr const char* kBluezBusName = "org.bluez";
    constexpr const char* kAppPath = "/com/signlang/eyes/manager";
    constexpr const char* kServicePath = "/com/signlang/eyes/manager/service0";
    constexpr const char* kRxPath = "/com/signlang/eyes/manager/service0/rx";
    constexpr const char* kTxPath = "/com/signlang/eyes/manager/service0/tx";
    constexpr const char* kAdvertisementPath = "/com/signlang/eyes/manager/advertisement0";
    constexpr const char* kServiceUuid = "3b5f1000-4ad2-4f53-9a65-6f6d65796573";
    constexpr const char* kRxUuid = "3b5f1001-4ad2-4f53-9a65-6f6d65796573";
    constexpr const char* kTxUuid = "3b5f1002-4ad2-4f53-9a65-6f6d65796573";

    constexpr const char* kObjectManagerXml = R"XML(
      <node>
        <interface name="org.freedesktop.DBus.ObjectManager">
          <method name="GetManagedObjects">
            <arg name="objects" type="a{oa{sa{sv}}}" direction="out"/>
          </method>
        </interface>
      </node>
    )XML";

    constexpr const char* kServiceXml = R"XML(
      <node>
        <interface name="org.bluez.GattService1">
          <property name="UUID" type="s" access="read"/>
          <property name="Primary" type="b" access="read"/>
          <property name="Includes" type="ao" access="read"/>
        </interface>
      </node>
    )XML";

    constexpr const char* kCharacteristicXml = R"XML(
      <node>
        <interface name="org.bluez.GattCharacteristic1">
          <method name="ReadValue">
            <arg name="options" type="a{sv}" direction="in"/>
            <arg name="value" type="ay" direction="out"/>
          </method>
          <method name="WriteValue">
            <arg name="value" type="ay" direction="in"/>
            <arg name="options" type="a{sv}" direction="in"/>
          </method>
          <method name="StartNotify"/>
          <method name="StopNotify"/>
          <property name="UUID" type="s" access="read"/>
          <property name="Service" type="o" access="read"/>
          <property name="Value" type="ay" access="read"/>
          <property name="Flags" type="as" access="read"/>
        </interface>
      </node>
    )XML";

    constexpr const char* kAdvertisementXml = R"XML(
      <node>
        <interface name="org.bluez.LEAdvertisement1">
          <method name="Release"/>
          <property name="Type" type="s" access="read"/>
          <property name="ServiceUUIDs" type="as" access="read"/>
          <property name="LocalName" type="s" access="read"/>
          <property name="Discoverable" type="b" access="read"/>
        </interface>
      </node>
    )XML";

    class GErrorGuard {
    public:
      ~GErrorGuard() {
        if (error != nullptr) {
          g_error_free(error);
        }
      }

      GError* error{nullptr};
    };

    auto parse_node(const char* xml) -> GDBusNodeInfo* {
      auto error = GErrorGuard{};
      auto* node = g_dbus_node_info_new_for_xml(xml, &error.error);
      if (node == nullptr) {
        throw std::runtime_error(std::string{"Failed to parse GDBus introspection XML: "} + error.error->message);
      }
      return node;
    }

    auto bytes_from_variant(GVariant* value) -> std::vector<std::uint8_t> {
      gsize size = 0;
      const auto* data =
          static_cast<const std::uint8_t*>(g_variant_get_fixed_array(value, &size, sizeof(std::uint8_t)));
      if (data == nullptr || size == 0) {
        return {};
      }
      return std::vector<std::uint8_t>{data, data + size};
    }

    auto variant_from_bytes(const std::vector<std::uint8_t>& value) -> GVariant* {
      return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, value.data(), value.size(), sizeof(std::uint8_t));
    }

    void add_service_interface(GVariantBuilder* ifaces) {
      auto props = GVariantBuilder{};
      g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&props, "{sv}", "UUID", g_variant_new_string(kServiceUuid));
      g_variant_builder_add(&props, "{sv}", "Primary", g_variant_new_boolean(TRUE));
      g_variant_builder_add(&props, "{sv}", "Includes", g_variant_new_objv(nullptr, 0));
      g_variant_builder_add(ifaces, "{sa{sv}}", "org.bluez.GattService1", &props);
    }

    void add_characteristic_interface(GVariantBuilder* ifaces, const char* uuid, const char* const* flags,
                                      std::size_t flag_count) {
      auto props = GVariantBuilder{};
      g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&props, "{sv}", "UUID", g_variant_new_string(uuid));
      g_variant_builder_add(&props, "{sv}", "Service", g_variant_new_object_path(kServicePath));
      g_variant_builder_add(&props, "{sv}", "Value", variant_from_bytes({}));
      g_variant_builder_add(&props, "{sv}", "Flags", g_variant_new_strv(flags, static_cast<gssize>(flag_count)));
      g_variant_builder_add(ifaces, "{sa{sv}}", "org.bluez.GattCharacteristic1", &props);
    }

    auto build_managed_objects() -> GVariant* {
      auto objects = GVariantBuilder{};
      g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));

      auto service_ifaces = GVariantBuilder{};
      g_variant_builder_init(&service_ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
      add_service_interface(&service_ifaces);
      g_variant_builder_add(&objects, "{oa{sa{sv}}}", kServicePath, &service_ifaces);

      constexpr const char* rx_flags[] = {"write", "write-without-response"};
      auto rx_ifaces = GVariantBuilder{};
      g_variant_builder_init(&rx_ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
      add_characteristic_interface(&rx_ifaces, kRxUuid, rx_flags, std::size(rx_flags));
      g_variant_builder_add(&objects, "{oa{sa{sv}}}", kRxPath, &rx_ifaces);

      constexpr const char* tx_flags[] = {"notify", "read"};
      auto tx_ifaces = GVariantBuilder{};
      g_variant_builder_init(&tx_ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
      add_characteristic_interface(&tx_ifaces, kTxUuid, tx_flags, std::size(tx_flags));
      g_variant_builder_add(&objects, "{oa{sa{sv}}}", kTxPath, &tx_ifaces);

      return g_variant_new("(a{oa{sa{sv}}})", &objects);
    }

    auto empty_options() -> GVariant* {
      auto options = GVariantBuilder{};
      g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
      return g_variant_new("(a{sv})", &options);
    }

    auto object_manager_method_call(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                    const gchar* method_name, GVariant*, GDBusMethodInvocation* invocation, gpointer)
        -> void {
      if (g_strcmp0(method_name, "GetManagedObjects") == 0) {
        g_dbus_method_invocation_return_value(invocation, build_managed_objects());
        return;
      }
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                            "Unknown ObjectManager method");
    }

    auto service_get_property(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property_name,
                              GError**, gpointer) -> GVariant* {
      if (g_strcmp0(property_name, "UUID") == 0) {
        return g_variant_new_string(kServiceUuid);
      }
      if (g_strcmp0(property_name, "Primary") == 0) {
        return g_variant_new_boolean(TRUE);
      }
      if (g_strcmp0(property_name, "Includes") == 0) {
        return g_variant_new_objv(nullptr, 0);
      }
      return nullptr;
    }

    auto characteristic_get_property(GDBusConnection*, const gchar*, const gchar* object_path, const gchar*,
                                     const gchar* property_name, GError**, gpointer) -> GVariant* {
      if (g_strcmp0(property_name, "UUID") == 0) {
        return g_variant_new_string(g_strcmp0(object_path, kRxPath) == 0 ? kRxUuid : kTxUuid);
      }
      if (g_strcmp0(property_name, "Service") == 0) {
        return g_variant_new_object_path(kServicePath);
      }
      if (g_strcmp0(property_name, "Value") == 0) {
        return variant_from_bytes({});
      }
      if (g_strcmp0(property_name, "Flags") == 0) {
        if (g_strcmp0(object_path, kRxPath) == 0) {
          constexpr const char* flags[] = {"write", "write-without-response"};
          return g_variant_new_strv(flags, std::size(flags));
        }
        constexpr const char* flags[] = {"notify", "read"};
        return g_variant_new_strv(flags, std::size(flags));
      }
      return nullptr;
    }

    auto advertisement_get_property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                    const gchar* property_name, GError**, gpointer user_data) -> GVariant* {
      const auto* server = static_cast<const BluetoothGattServer*>(user_data);
      if (g_strcmp0(property_name, "Type") == 0) {
        return g_variant_new_string("peripheral");
      }
      if (g_strcmp0(property_name, "ServiceUUIDs") == 0) {
        constexpr const char* uuids[] = {kServiceUuid};
        return g_variant_new_strv(uuids, std::size(uuids));
      }
      if (g_strcmp0(property_name, "LocalName") == 0) {
        return g_variant_new_string(server == nullptr ? "SignLang Eyes" : server->local_name().c_str());
      }
      if (g_strcmp0(property_name, "Discoverable") == 0) {
        return g_variant_new_boolean(TRUE);
      }
      return nullptr;
    }

    auto characteristic_method_call(GDBusConnection*, const gchar*, const gchar* object_path, const gchar*,
                                    const gchar* method_name, GVariant* parameters, GDBusMethodInvocation* invocation,
                                    gpointer user_data) -> void {
      auto* server = static_cast<BluetoothGattServer*>(user_data);
      if (g_strcmp0(object_path, kRxPath) == 0 && g_strcmp0(method_name, "WriteValue") == 0) {
        auto* value = g_variant_get_child_value(parameters, 0);
        const auto bytes = bytes_from_variant(value);
        g_variant_unref(value);
        server->handle_write_value(bytes);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
      }

      if (g_strcmp0(object_path, kTxPath) == 0 && g_strcmp0(method_name, "ReadValue") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ay)", variant_from_bytes({})));
        return;
      }

      if (g_strcmp0(object_path, kTxPath) == 0 && g_strcmp0(method_name, "StartNotify") == 0) {
        const auto* sender = g_dbus_method_invocation_get_sender(invocation);
        if (!server->request_start_notify(sender)) {
          g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                                "Another BLE client is already subscribed to handpose streaming");
          return;
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
      }

      if (g_strcmp0(object_path, kTxPath) == 0 && g_strcmp0(method_name, "StopNotify") == 0) {
        const auto* sender = g_dbus_method_invocation_get_sender(invocation);
        server->request_stop_notify(sender);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
      }

      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                            "Unknown characteristic method");
    }

    auto advertisement_method_call(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method_name,
                                   GVariant*, GDBusMethodInvocation* invocation, gpointer) -> void {
      if (g_strcmp0(method_name, "Release") == 0) {
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
      }
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                            "Unknown advertisement method");
    }

    void name_owner_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                            GVariant* parameters, gpointer user_data) {
      auto* server = static_cast<BluetoothGattServer*>(user_data);
      const char* name = nullptr;
      const char* old_owner = nullptr;
      const char* new_owner = nullptr;
      g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
      if (name != nullptr && new_owner != nullptr && new_owner[0] == '\0') {
        server->release_notify_owner_if_matches(name);
      }
    }

    const auto kObjectManagerVtable = GDBusInterfaceVTable{object_manager_method_call, nullptr, nullptr};
    const auto kServiceVtable = GDBusInterfaceVTable{nullptr, service_get_property, nullptr};
    const auto kCharacteristicVtable =
        GDBusInterfaceVTable{characteristic_method_call, characteristic_get_property, nullptr};
    const auto kAdvertisementVtable =
        GDBusInterfaceVTable{advertisement_method_call, advertisement_get_property, nullptr};

  } // namespace

  BluetoothGattServer::BluetoothGattServer(BluetoothGattOptions options) : options_{std::move(options)} {
    if (options_.max_notify_payload == 0) {
      throw std::runtime_error("BLE max notify payload must be greater than zero");
    }
  }

  BluetoothGattServer::~BluetoothGattServer() { stop(); }

  auto BluetoothGattServer::max_notify_payload() const -> std::uint32_t { return options_.max_notify_payload; }

  auto BluetoothGattServer::local_name() const -> const std::string& { return options_.local_name; }

  void BluetoothGattServer::start(PacketHandler handler) {
    std::unique_lock lock(mutex_);
    if (started_.load()) {
      return;
    }
    handler_ = std::move(handler);

    try {
      auto error = GErrorGuard{};
      connection_ = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error.error);
      if (connection_ == nullptr) {
        throw std::runtime_error(std::string{"Failed to connect to system D-Bus: "} + error.error->message);
      }

      loop_ = g_main_loop_new(nullptr, FALSE);
      object_manager_node_ = parse_node(kObjectManagerXml);
      service_node_ = parse_node(kServiceXml);
      characteristic_node_ = parse_node(kCharacteristicXml);
      advertisement_node_ = parse_node(kAdvertisementXml);

      register_objects();
      ensure_adapter_powered();

      started_.store(true);
      loop_thread_ = std::thread{[this]() { run_loop(); }};
      for (auto attempt = 0; attempt < 1000 && !g_main_loop_is_running(loop_); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
      if (!g_main_loop_is_running(loop_)) {
        throw std::runtime_error("Timed out waiting for BLE D-Bus main loop to start");
      }

      register_with_bluez();
    } catch (...) {
      unregister_from_bluez();
      unregister_objects();
      if (loop_ != nullptr) {
        g_main_loop_quit(loop_);
      }
      started_.store(false);
      notify_owner_.clear();
      notifications_enabled_.store(false);
      handler_ = {};

      lock.unlock();
      if (loop_thread_.joinable()) {
        loop_thread_.join();
      }
      lock.lock();

      release_local_resources();
      throw;
    }
  }

  void BluetoothGattServer::stop() {
    {
      std::lock_guard lock(mutex_);
      if (!started_.load()) {
        return;
      }
      unregister_from_bluez();
      unregister_objects();
      if (loop_ != nullptr) {
        g_main_loop_quit(loop_);
      }
      started_.store(false);
    }

    if (loop_thread_.joinable()) {
      loop_thread_.join();
    }

    release_local_resources();
  }

  void BluetoothGattServer::register_objects() {
    auto error = GErrorGuard{};
    object_registration_ids_.push_back(
        g_dbus_connection_register_object(connection_, kAppPath, object_manager_node_->interfaces[0],
                                          &kObjectManagerVtable, this, nullptr, &error.error));
    if (object_registration_ids_.back() == 0) {
      throw std::runtime_error(std::string{"Failed to register ObjectManager object: "} + error.error->message);
    }

    object_registration_ids_.push_back(g_dbus_connection_register_object(
        connection_, kServicePath, service_node_->interfaces[0], &kServiceVtable, this, nullptr, &error.error));
    if (object_registration_ids_.back() == 0) {
      throw std::runtime_error(std::string{"Failed to register GATT service object: "} + error.error->message);
    }

    object_registration_ids_.push_back(
        g_dbus_connection_register_object(connection_, kRxPath, characteristic_node_->interfaces[0],
                                          &kCharacteristicVtable, this, nullptr, &error.error));
    if (object_registration_ids_.back() == 0) {
      throw std::runtime_error(std::string{"Failed to register GATT RX characteristic object: "} +
                               error.error->message);
    }

    object_registration_ids_.push_back(
        g_dbus_connection_register_object(connection_, kTxPath, characteristic_node_->interfaces[0],
                                          &kCharacteristicVtable, this, nullptr, &error.error));
    if (object_registration_ids_.back() == 0) {
      throw std::runtime_error(std::string{"Failed to register GATT TX characteristic object: "} +
                               error.error->message);
    }

    object_registration_ids_.push_back(
        g_dbus_connection_register_object(connection_, kAdvertisementPath, advertisement_node_->interfaces[0],
                                          &kAdvertisementVtable, this, nullptr, &error.error));
    if (object_registration_ids_.back() == 0) {
      throw std::runtime_error(std::string{"Failed to register BLE advertisement object: "} + error.error->message);
    }

    name_owner_watch_id_ = g_dbus_connection_signal_subscribe(
        connection_, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus",
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE, name_owner_changed, this, nullptr);
  }

  void BluetoothGattServer::unregister_objects() {
    if (name_owner_watch_id_ != 0 && connection_ != nullptr) {
      g_dbus_connection_signal_unsubscribe(connection_, name_owner_watch_id_);
      name_owner_watch_id_ = 0;
    }
    for (const auto registration_id : object_registration_ids_) {
      if (registration_id != 0 && connection_ != nullptr) {
        g_dbus_connection_unregister_object(connection_, registration_id);
      }
    }
    object_registration_ids_.clear();
  }

  void BluetoothGattServer::ensure_adapter_powered() {
    auto read_powered = [&]() {
      auto error = GErrorGuard{};
      auto* result = g_dbus_connection_call_sync(
          connection_, kBluezBusName, options_.adapter_path.c_str(), "org.freedesktop.DBus.Properties", "Get",
          g_variant_new("(ss)", "org.bluez.Adapter1", "Powered"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 5000,
          nullptr, &error.error);
      if (result == nullptr) {
        throw std::runtime_error(std::string{"Failed to read BlueZ adapter Powered property: "} + error.error->message);
      }

      GVariant* value = nullptr;
      g_variant_get(result, "(@v)", &value);
      auto* inner = g_variant_get_variant(value);
      const auto powered = g_variant_get_boolean(inner);
      g_variant_unref(inner);
      g_variant_unref(value);
      g_variant_unref(result);
      return powered != FALSE;
    };

    if (read_powered()) {
      return;
    }

    spdlog::info("BlueZ adapter {} is powered off; enabling it", options_.adapter_path);
    auto error = GErrorGuard{};
    auto* result = g_dbus_connection_call_sync(
        connection_, kBluezBusName, options_.adapter_path.c_str(), "org.freedesktop.DBus.Properties", "Set",
        g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered", g_variant_new_boolean(TRUE)), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error.error);
    if (result == nullptr) {
      throw std::runtime_error(std::string{"Failed to enable BlueZ adapter: "} + error.error->message);
    }
    g_variant_unref(result);

    if (!read_powered()) {
      throw std::runtime_error("BlueZ adapter is still powered off after enabling it");
    }
  }

  void BluetoothGattServer::register_with_bluez() {
    auto error = GErrorGuard{};
    auto options = GVariantBuilder{};
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));

    auto* result = g_dbus_connection_call_sync(
        connection_, kBluezBusName, options_.adapter_path.c_str(), "org.bluez.GattManager1", "RegisterApplication",
        g_variant_new("(oa{sv})", kAppPath, &options), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error.error);
    if (result == nullptr) {
      throw std::runtime_error(std::string{"Failed to register BLE GATT application with BlueZ: "} +
                               error.error->message);
    }
    g_variant_unref(result);

    auto adv_options = GVariantBuilder{};
    g_variant_builder_init(&adv_options, G_VARIANT_TYPE("a{sv}"));
    result = g_dbus_connection_call_sync(connection_, kBluezBusName, options_.adapter_path.c_str(),
                                         "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement",
                                         g_variant_new("(oa{sv})", kAdvertisementPath, &adv_options), nullptr,
                                         G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error.error);
    if (result == nullptr) {
      throw std::runtime_error(std::string{"Failed to register BLE advertisement with BlueZ: "} + error.error->message);
    }
    g_variant_unref(result);
    spdlog::info("BLE GATT service registered on {}", options_.adapter_path);
  }

  void BluetoothGattServer::unregister_from_bluez() {
    if (connection_ == nullptr) {
      return;
    }

    {
      auto error = GErrorGuard{};
      auto* result = g_dbus_connection_call_sync(connection_, kBluezBusName, options_.adapter_path.c_str(),
                                                 "org.bluez.LEAdvertisingManager1", "UnregisterAdvertisement",
                                                 g_variant_new("(o)", kAdvertisementPath), nullptr,
                                                 G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error.error);
      if (result != nullptr) {
        g_variant_unref(result);
      }
    }

    {
      auto error = GErrorGuard{};
      auto* result = g_dbus_connection_call_sync(
          connection_, kBluezBusName, options_.adapter_path.c_str(), "org.bluez.GattManager1", "UnregisterApplication",
          g_variant_new("(o)", kAppPath), nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error.error);
      if (result != nullptr) {
        g_variant_unref(result);
      }
    }
  }

  void BluetoothGattServer::run_loop() { g_main_loop_run(loop_); }

  void BluetoothGattServer::release_local_resources() {
    if (advertisement_node_ != nullptr) {
      g_dbus_node_info_unref(advertisement_node_);
      advertisement_node_ = nullptr;
    }
    if (characteristic_node_ != nullptr) {
      g_dbus_node_info_unref(characteristic_node_);
      characteristic_node_ = nullptr;
    }
    if (service_node_ != nullptr) {
      g_dbus_node_info_unref(service_node_);
      service_node_ = nullptr;
    }
    if (object_manager_node_ != nullptr) {
      g_dbus_node_info_unref(object_manager_node_);
      object_manager_node_ = nullptr;
    }
    if (loop_ != nullptr) {
      g_main_loop_unref(loop_);
      loop_ = nullptr;
    }
    if (connection_ != nullptr) {
      g_object_unref(connection_);
      connection_ = nullptr;
    }
  }

  auto BluetoothGattServer::request_start_notify(const char* sender) -> bool {
    const auto owner = std::string{sender == nullptr ? "" : sender};
    std::lock_guard lock(mutex_);
    if (!notify_owner_.empty() && notify_owner_ != owner) {
      return false;
    }

    notify_owner_ = owner;
    notifications_enabled_.store(true);
    spdlog::info("BLE handpose stream subscribed by {}", notify_owner_.empty() ? "<unknown>" : notify_owner_);
    return true;
  }

  void BluetoothGattServer::request_stop_notify(const char* sender) {
    const auto owner = std::string{sender == nullptr ? "" : sender};
    std::lock_guard lock(mutex_);
    if (!notify_owner_.empty() && notify_owner_ != owner) {
      return;
    }

    spdlog::info("BLE handpose stream unsubscribed by {}", notify_owner_.empty() ? "<unknown>" : notify_owner_);
    notify_owner_.clear();
    notifications_enabled_.store(false);
  }

  void BluetoothGattServer::release_notify_owner_if_matches(const char* owner_name) {
    const auto owner = std::string{owner_name == nullptr ? "" : owner_name};
    std::lock_guard lock(mutex_);
    if (notify_owner_.empty() || notify_owner_ != owner) {
      return;
    }

    spdlog::info("BLE handpose stream owner {} disappeared; releasing subscription", notify_owner_);
    notify_owner_.clear();
    notifications_enabled_.store(false);
  }

  void BluetoothGattServer::handle_write_value(const std::vector<std::uint8_t>& value) {
    PacketHandler handler;
    {
      std::lock_guard lock(mutex_);
      handler = handler_;
    }

    if (!handler) {
      return;
    }

    const auto response = handler(value);
    if (!response.empty()) {
      notify_packet(response);
    }
  }

  auto BluetoothGattServer::notifications_enabled() const -> bool { return notifications_enabled_.load(); }

  void BluetoothGattServer::notify_packet(const std::vector<std::uint8_t>& packet) {
    if (!notifications_enabled_.load()) {
      return;
    }

    const auto chunk_size = static_cast<std::size_t>(std::max<std::uint32_t>(1, options_.max_notify_payload));
    for (std::size_t offset = 0; offset < packet.size(); offset += chunk_size) {
      const auto end = std::min(packet.size(), offset + chunk_size);
      auto chunk = std::vector<std::uint8_t>{packet.begin() + static_cast<std::ptrdiff_t>(offset),
                                             packet.begin() + static_cast<std::ptrdiff_t>(end)};
      emit_tx_value_changed(chunk);
    }
  }

  void BluetoothGattServer::emit_tx_value_changed(const std::vector<std::uint8_t>& value) {
    if (connection_ == nullptr) {
      return;
    }

    auto changed = GVariantBuilder{};
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", "Value", variant_from_bytes(value));

    auto invalidated = GVariantBuilder{};
    g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));

    const auto emitted = g_dbus_connection_emit_signal(
        connection_, nullptr, kTxPath, "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(sa{sv}as)", "org.bluez.GattCharacteristic1", &changed, &invalidated), nullptr);
    if (!emitted) {
      spdlog::warn("Failed to emit BLE TX notification");
    }
  }

} // namespace signlang::signlang_manager
