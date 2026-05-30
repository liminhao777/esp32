# ESP32 BLE 配网模板说明

文件: [main/ble_prov_template.c](main/ble_prov_template.c)

简介:
- 这是一个精简的 BLE 配网示例，演示了推荐的初始化顺序、受保护的 `esp_netif` 创建、`wifi_prov_mgr`（BLE 方案）、自定义 BLE endpoint、以及 DHCP 看门狗的占位实现。

使用说明（快速）:
- 确保 `sdkconfig` 启用了 NimBLE 和 wifi_prov_mgr：
  - `CONFIG_BT_ENABLED=y`
  - `CONFIG_BT_NIMBLE_ENABLED=y`
  - `CONFIG_WIFI_PROV_AUTOSTOP_TIMEOUT` 可按需设置（示例为 30s）
- 推荐使用较大 app 分区（例如 partitions_singleapp_large.csv），避免编译时溢出。
- 将 `ble_prov_template.c` 作为 `main.c` 的参考：根据项目需要替换自定义 endpoint、POP（proof-of-possession）和 BSSID 锁定实现。

测试步骤:
1. 编译并刷写固件：
```powershell
idf.py build
idf.py -p COMx flash
``` 
2. 使用支持 ESP BLE 配网的手机 App（例如 Espressif 的样例 App）扫描设备并填写 SSID/密码。
3. 观察串口日志：等待 `Got IP address` 输出以确认 DHCP 成功。

注意事项:
- 模板中 `dhcp_watchdog_cb` 与 2.4GHz BSSID 锁定为占位实现；请根据你的网络环境实现 `scan -> pick 2.4GHz AP -> set bssid -> connect` 的逻辑。
- 不要在 `IP_EVENT_STA_GOT_IP` 回调中立即 `wifi_prov_mgr_deinit()`；建议等待手机 App 收到成功 ACK 后再清理，以免中断配网流程。

如需我将这个模板集成到项目的 `main.c` 中或为你的具体业务（例如 JSON 字段解析、设备绑定）补充实现，请告诉我下一步需求。
