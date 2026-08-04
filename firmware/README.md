# Firmware Assets

本目录存放烧录固件的辅助资产（bootloader）与完整镜像生成说明。

## Bootloader

`adafruit_bootloader_nosd.hex` — Adafruit nRF52 Bootloader（ZMK 变体，无 softdevice），
应用偏移 0x26000。

- 来源：https://github.com/joric/nrfmicro/releases/tag/1.4
  原始文件：`nrfmicro_nrf52840_bootloader-0.8.0-dirty_nosd.hex`
- 用途：合并生成完整烧录镜像（bootloader + 应用）；也可单独用 J-Link 恢复 bootloader。
- 覆盖区域：0x00000-0x00B00（引导向量表头）、0xF4000-0xFC484（bootloader 主体）、
  0xFD800-0xFD858（设置区）、0x10001014-0x1000101C（UICR）。

## 完整镜像生成

GitHub Actions 构建时通过 `tools/merge_hex.py` 将上述 bootloader 与应用固件
（`thinkpad_wireless-zmk.hex`，起始 0x26000）合并为 `thinkpad_full_0x0000.hex`
（覆盖 0x0 起完整布局，全片烧录用，可勾选 "Erase all"）。

本地手动合并：

```sh
python3 tools/merge_hex.py output_full.hex \
    firmware/adafruit_bootloader_nosd.hex \
    <应用hex>
```
