# 代码审查记录：板级模块迁移

审查日期：2026-07-10

审查范围：工作区中未提交的板级目录迁移、`zephyr/module.yml` 与 `config/west.yml` 变更；重点检查 `thinkpad_wireless` 板在 ZMK/Zephyr 中的发现、配置与持续集成路径。

## 结论

当前改动不能稳定构建。存在两个 P0 阻断问题：迁移后的板目录没有被 Zephyr 构建系统注册，且依赖版本降至与板定义格式不兼容的 ZMK/Zephyr 组合。应先修复两项 P0，再进行干净环境的固件构建验证。

## 第一轮：构建系统与目录发现

### P0：`module.yml` 没有注册板根目录

- 位置：[zephyr/module.yml](../zephyr/module.yml) 第 4-5 行。
- 现象：`build.boards` 是 Twister 测试发现的字段，不会将 `module/boards` 加入 Zephyr 的 `BOARD_ROOT`。迁移后，`thinkpad_wireless` 不再位于原来的配置板目录，构建系统因而无法查找到该板。
- 影响：`west build -b thinkpad_wireless` 或云端构建在板选择阶段失败，后续 DTS、Kconfig 和 C 源文件都不会参与构建。
- 建议：在 `build.settings` 下使用 `board_root: module`，因为模块根目录下存在 `module/boards`。如仍需 Twister 识别，可保留顶层 `boards: [module/boards]`，但不能以它替代 `board_root`。

建议结构：

```yaml
build:
  cmake: module
  kconfig: module/Kconfig
  settings:
    board_root: module
boards:
  - module/boards
```

依据：Zephyr 的模块文档将 `board_root` 定义为“包含额外板定义的构建设置”，且要求板位于 `<board_root>/boards`；文档中顶层 `boards` 仅用于 Twister 的测试板发现。[Zephyr Modules](https://docs.zephyrproject.org/latest/develop/modules.html)

## 第二轮：依赖与硬件模型兼容性

### P0：将 ZMK 固定为 `v0.3` 与 HWMv2 板定义不兼容

- 位置：[config/west.yml](../config/west.yml) 第 8 行、[module/boards/thinkpad_wireless/board.yml](../module/boards/thinkpad_wireless/board.yml) 第 1-5 行。
- 现象：当前板使用 HWMv2 结构（`board.yml`、`Kconfig.board` 等）。ZMK `v0.3` 使用 Zephyr 3.5；而 HWMv2 从 Zephyr 3.7 才引入，且没有对旧模型的直接向后兼容。
- 影响：即使修复板根目录，Zephyr 3.5 也无法按当前 HWMv2 定义解析板，构建仍会失败或忽略必要的板元数据。
- 建议：不要将该项目切换到 `v0.3`。恢复至此前可用的 `main`，或固定到一个已验证、且引入 Zephyr 3.7 及以上版本的 ZMK 提交/发布版本。随后用干净工作区执行完整构建。

依据：Zephyr 官方的 HWMv2 迁移说明指出新硬件模型在 3.6 发布后引入，并从 3.7 起生效；旧模型没有直接兼容层。[Zephyr Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)；ZMK `v0.3` 的兼容模块说明标注其目标为 Zephyr 3.5。[prospector-zmk-module](https://github.com/carrefinho/prospector-zmk-module)

## 第三轮：可维护性、配置一致性与防回归

### P1：目录迁移后仍保留旧路径说明

- 位置：[config/thinkpad_wireless.conf](../config/thinkpad_wireless.conf) 第 6 行。
- 现象：注释仍指向已删除的 `config/boards/thinkpad_wireless/thinkpad_wireless_defconfig`。
- 影响：后续维护者会在错误位置查找硬件默认配置，容易在旧目录重新创建文件并导致配置来源分裂。
- 建议：改为 `module/boards/thinkpad_wireless/thinkpad_wireless_defconfig`，并同步搜索项目文档中是否还有旧目录说明。

### P1：迁移缺少自动构建门禁

- 位置：[.github/workflows/build.yml](../.github/workflows/build.yml) 第 3 行。
- 现象：现有工作流仅支持手动触发；本次会影响板发现和依赖解析的迁移无法在提交或拉取请求时自动验证。
- 影响：P0 类配置回归会在手动发布固件时才暴露。
- 建议：为工作流增加 `push` 与 `pull_request` 触发条件，并至少覆盖 `build.yaml` 中的 `thinkpad_wireless` 构建矩阵。依赖锁定或目录迁移后，应执行一次 pristine build。

## 已检查项与验证边界

- 板级九个文件已从 `config/boards/thinkpad_wireless` 移至 `module/boards/thinkpad_wireless`；迁移后 CMake 的模块头文件相对路径指向正确的 `module/include`。
- `git diff --check` 未发现空白符错误。
- 未执行完整固件构建：当前工作区不含已初始化的 ZMK/Zephyr 依赖工作区，且两项 P0 在构建前就已确定会阻断板发现/解析。

## 修复后的建议验证

```powershell
west update
west build -p always -s zmk/app -b thinkpad_wireless -- -DZMK_CONFIG=$PWD/config
```

验证应确认：板可被发现、DTS/Kconfig 均被加载、`board.c` 与 `status_leds.c` 进入编译目标，并产出 `zephyr.uf2` 或目标烧录格式的固件。
