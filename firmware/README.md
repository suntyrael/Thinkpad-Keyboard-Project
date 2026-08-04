# Firmware Assets

本目录存放烧录固件的辅助资产（bootloader）与完整镜像生成说明。

## Bootloader

`adafruit_bootloader_zmk.hex` — Adafruit nRF52 Bootloader（**ZMK 布局变体**，
应用起始地址 0x26000，s140 版）。

- 来源：https://nicekeyboards.com/assets/nice_nano_bootloader-0.6.0_s140_6.1.1.hex
  （nice!nano 官方 bootloader，ZMK 生态标准）
- 用途：合并生成完整烧录镜像（bootloader + 应用）；也可单独用 J-Link 恢复 bootloader。
- 覆盖区域：0x00000-0x00B00（引导向量表头）、0xF4000-0x100000（bootloader 主体）、
  0x10001014-0x1000101C（UICR）。
- **注意**：不要使用 nrfmicro 的 nosd 变体（`nrfmicro_nrf52840_bootloader-*-nosd.hex`）——
  其无 SoftDevice 时应用起始地址为 0x1000，与 ZMK 的 0x26000 布局不兼容，会导致
  bootloader 判定应用无效并停留在 DFU 模式（插入 USB 弹出升级盘）。

## 完整镜像生成

GitHub Actions 构建时通过 `tools/merge_hex.py` 将上述 bootloader 与应用固件
（`thinkpad_wireless-zmk.hex`，起始 0x26000）合并为 `thinkpad_full_0x0000.hex`
（覆盖 0x0 起完整布局，全片烧录用，可勾选 "Erase all"）。

本地手动合并：

```sh
python3 tools/merge_hex.py output_full.hex \
    firmware/adafruit_bootloader_zmk.hex \
    <应用hex>
```

## 烧录注意事项（重要）

1. 完整镜像（`thinkpad_full_0x0000.hex`）烧录时必须勾选 **"Erase all"**。
2. 完整镜像已通过 `tools/merge_hex.py --mark-app-valid` 在 bootloader settings 页（0xFF000）
   写入 `bank_0 = BANK_VALID_APP (0x0001)` 标记。**这是 J-Link 直接烧录后能正常启动的关键**——
   bootloader 依赖该标记判定应用有效；缺失时（settings 页保持擦除态）会判定应用无效并
   停留在 UF2 模式（弹 U盘）。UF2 拖拽流程会自动写该标记，J-Link 直烧不会。
3. 若手动烧录旧版完整镜像（无标记），可执行：
   `nrfutil device write --address 0xFF000 --value 0x00000001`
