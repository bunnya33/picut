## v2.11.3
> 2026.01.18

* [专业版] 支持自定义部分应用内快捷键 (首选项 - 控制 - 应用内快捷键) [#2189](https://github.com/Snipaste/feedback/issues/2189) [#3317](https://github.com/Snipaste/feedback/issues/3317) [#3584](https://github.com/Snipaste/feedback/issues/3584)
* [专业版] 文字识别：支持腾讯和 OCR.space 接口
* 其它问题修复和细节完善 [#3572](https://github.com/Snipaste/feedback/issues/3572) [#3579](https://github.com/Snipaste/feedback/issues/3579) [#3580](https://github.com/Snipaste/feedback/issues/3580) [#3597](https://github.com/Snipaste/feedback/issues/3597)

## v2.11.2
> 2025.12.15

* 细节改进

## v2.11.1
> 2025.12.13

* 修复了一些已知问题 [#3551](https://github.com/Snipaste/feedback/issues/3551) [#3566](https://github.com/Snipaste/feedback/issues/3566)

## v2.11
> 2025.12.08

* [专业版] 支持文字识别 (OCR) [#188](https://github.com/Snipaste/feedback/issues/188)
* [专业版] 新增截屏的命令行选项 `--hold`, 带指定输出时可以在退出截屏前进行标注
* [专业版] 新增命令行选项 `--block [SECONDS]`, 可以让命令行调用方在等待截图完成后再退出
* [专业版] 可自定义贴图选区缩略图的快捷键
* [专业版] 截图时允许使用更大的圆角半径
* 支持 Windows ARM64
* 支持 HDR 色彩校正 [#1112](https://github.com/Snipaste/feedback/issues/1112)
* 优化显示器连接/断开的检测
* 改进 Windows 11 的界面元素检测
* 关闭或销毁多个贴图前增加二次确认
* 支持 <kbd>Shift</kbd> + 鼠标滚轮 对选中的矩形/椭圆标注进行缩放
* 修复特定场景下放大镜不重绘的问题
* [Mac/Linux] 修复右键点击非活动贴图窗口时不弹出菜单的问题
* [Linux] 解决与 KDE Plasma 6.5 的剪贴板兼容性问题 [#3528](https://github.com/Snipaste/feedback/issues/3528)
* 其它问题修复和细节完善 [#3296](https://github.com/Snipaste/feedback/issues/3296) [#3414](https://github.com/Snipaste/feedback/issues/3414) [#3435](https://github.com/Snipaste/feedback/issues/3435) [#3441](https://github.com/Snipaste/feedback/issues/3441) [#3507](https://github.com/Snipaste/feedback/issues/3507) [#3535](https://github.com/Snipaste/feedback/issues/3535)

## v2.10.8
> 2025.06.03

* 修复错误的气泡通知 [#3356](https://github.com/Snipaste/feedback/issues/3356)

## v2.10.7
> 2025.05.31

* 增加命令行选项 `snip --active-screen`
* 关闭多个贴图时增加二次确认 [#3303](https://github.com/Snipaste/feedback/issues/3303)
* 其它问题修复和细节完善 [#3209](https://github.com/Snipaste/feedback/issues/3209) [#3304](https://github.com/Snipaste/feedback/issues/3304) [#3306](https://github.com/Snipaste/feedback/issues/3306) [#3309](https://github.com/Snipaste/feedback/issues/3309) [#3328](https://github.com/Snipaste/feedback/issues/3328) [#3332](https://github.com/Snipaste/feedback/issues/3332)

## v2.10.6
> 2025.03.19

* [Mac] 新选项：可设置取色器的色彩空间，默认为 sRGB [#2002](https://github.com/Snipaste/feedback/issues/2002)
* [Mac] 修复保存成 PNG/WebP 的色差问题
* [专业版] 改善深色背景下的二维码识别 [#3233](https://github.com/Snipaste/feedback/issues/3233)
* OpenSSL 升级到 3.4.1 版本
* 其它问题修复和细节完善 [#3195](https://github.com/Snipaste/feedback/issues/3195) [#3221](https://github.com/Snipaste/feedback/issues/3221) [#3251](https://github.com/Snipaste/feedback/issues/3251)

## v2.10.5
> 2025.01.24

* 修复选项 `忽略其他程序模拟的模拟的"复制"命令` 并默认启用 [#3195](https://github.com/Snipaste/feedback/issues/3195)

## v2.10.4
> 2025.01.17

* [专业版] 取色器支持显示 HSV 等色值
* [专业版] 支持绑定按键到自定义输出选项
* 增加新选项：禁用辅助线
* 白板模式下显示更干净的界面
* 为截图写入相关元信息以供 iCloud 分类
* 当屏幕设置改动导致部分贴图不可见时：
  * 不再自动将屏幕外的贴图移回屏幕内，以便屏幕设置恢复时贴图布局保持不变
  * 可以从托盘图标菜单选择"恢复屏幕外的贴图"
* 修复部分 Linux 环境下截图闪屏问题 [#3158](https://github.com/Snipaste/feedback/issues/3158)
* 修复部分情况下放大镜未刷新 [#3145](https://github.com/Snipaste/feedback/issues/3145)
* 修复部分情况下工具条未显示
* 改善对划词软件的兼容
* OpenSSL 升级到 3.4.0 版本 [#3190](https://github.com/Snipaste/feedback/issues/3190)
* 其它问题修复和细节完善 [#3153](https://github.com/Snipaste/feedback/issues/3153) [#3155](https://github.com/Snipaste/feedback/issues/3155)

## v2.10.3
> 2024.11.28

* 支持离线激活桌面专业版
* [专业版] 增加[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE): `Interface/hide_output_options`
* [Win] 修复系统分享对话框未置顶显示
* [Mac] 修复超级截图的一些问题
* [Linux] 修复 Wayland 环境下配置文件无法持久化 [#2979](https://github.com/Snipaste/feedback/issues/2979)
* [Linux] 适配深色模式
* [Linux] 支持 qt6ct [#3061](https://github.com/Snipaste/feedback/issues/3061)
* 其它问题修复和细节完善 [#3070](https://github.com/Snipaste/feedback/issues/3070) [#3131](https://github.com/Snipaste/feedback/issues/3131) [#3142](https://github.com/Snipaste/feedback/issues/3142)

## v2.10.2
> 2024.09.23

* [Mac] 修复超级截图相关问题
* [Linux] 支持超级截图 [#3054](https://github.com/Snipaste/feedback/issues/3054)
* [专业版] 增加[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE): `Snip/animated_region_transition` [#3057](https://github.com/Snipaste/feedback/issues/3057)
* 其它问题修复和细节完善 [#3069](https://github.com/Snipaste/feedback/issues/3069)

## v2.10.1
> 2024.09.13

* 修复无法在 Windows 7 运行 [#3055](https://github.com/Snipaste/feedback/issues/3055)
* 修复一个导致崩溃的错误 [#3065](https://github.com/Snipaste/feedback/issues/3065)

## v2.10
> 2024.09.10

* 桌面正式版 (Windows/Mac/Linux) 发布！支持解锁[专业版](https://docs.snipaste.com/zh-cn/pro)！

## v2.9.2
> 2024.07.02

* [专业版] 记住上次截图的锁定纵横比 [#2990](https://github.com/Snipaste/feedback/issues/2990)
* [Linux] 修复在部分桌面环境无法弹出托盘菜单 [#2843](https://github.com/Snipaste/feedback/issues/2843) [#2952](https://github.com/Snipaste/feedback/issues/2952)
* [Linux] 对于使用 Wayland 的非 KDE/Gnome 桌面环境，使用 freedesktop 截图 [#2949](https://github.com/Snipaste/feedback/issues/2949)
* [Linux] 截图适配纵向显示器 [#2927](https://github.com/Snipaste/feedback/issues/2927)
* 修复对全屏窗口的区域检测
* 更好地兼容远程桌面/虚拟机
* 提高与 MyDockFinder 的兼容性 [#2939](https://github.com/Snipaste/feedback/issues/2939)
* OpenSSL 升级到 3.3.0 版本 [#2946](https://github.com/Snipaste/feedback/issues/2946)
* 其他问题修复和细节改善 [#2951](https://github.com/Snipaste/feedback/issues/2951) [#2988](https://github.com/Snipaste/feedback/issues/2988)

## v2.9.1
> 2024.05.08

* [专业版] 贴图支持 Windows 11 23H2 的虚拟桌面 [#2931](https://github.com/Snipaste/feedback/issues/2931)
* [专业版] 增加命令行选项 `-o silent`，支持静默保存截图到指定文件路径 [#2926](https://github.com/Snipaste/feedback/issues/2926)
* 修复复制全屏截图在特定条件下导致卡顿的问题

## v2.9
> 2024.04.24

* [Linux] 截屏支持 Wayland
* [Linux] 截屏支持捕捉光标图案 [#2849](https://github.com/Snipaste/feedback/issues/2849)
* [Mac] 修复保存图片为 WebP [#2765](https://github.com/Snipaste/feedback/issues/2765)
* 开/关鼠标穿透时显示文本提示 [#2791](https://github.com/Snipaste/feedback/issues/2791)
* 其他问题修复或改进 [#2733](https://github.com/Snipaste/feedback/issues/2733) [#2809](https://github.com/Snipaste/feedback/issues/2809) [#2817](https://github.com/Snipaste/feedback/issues/2817) [#2850](https://github.com/Snipaste/feedback/issues/2850) [#2901](https://github.com/Snipaste/feedback/issues/2901) [#2902](https://github.com/Snipaste/feedback/issues/2902)
* 增加立陶宛语翻译（感谢 [Džiugas Januševičius](mailto:dziugas1959@hotmail.comm)）

## v2.8.9 (Linux)
> 2024.01.08

* 适配多工作区 [#2816](https://github.com/Snipaste/feedback/issues/2816)
* 修复多屏环境下截屏区域错误 [#2819](https://github.com/Snipaste/feedback/issues/2819)
* 允许设置开机启动 [#2814](https://github.com/Snipaste/feedback/issues/2814)
* 允许贴图移出屏幕边缘
* 其他问题修复

## v2.8.8 Beta (Linux)
> 2023.12.28

* Linux 版首次发布

## v2.8.8
> 2023.09.25

* 修复在部分多屏环境下无法正常截屏

## v2.8.7
> 2023.09.23

* 修复在少数多屏环境下无法正常截屏
* 修复无法在 Windows 7 上运行

## v2.8.6
> 2023.09.20

* [专业版] 支持圆角矩形截图
* [专业版] 支持多组计数标注
* [专业版] 对于命令行指令 `snip --size WIDTH HEIGHT`，新增参数 `--keep_aspect_ratio`
* [专业版] 贴图支持 Windows 11 22H2 以上版本的虚拟桌面
* [专业版] 增加[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE): `Misc/software_metadata`
* 支持保存图像为 WebP 格式
* 支持对 Qt 5.11+ 应用进行界面元素检测
* 箭头标注增加 "开/关箭头" 图标按钮
* 修复读取 WebP 图片的安全漏洞 (CVE-2023-4863)
* 其他问题修复或改进 [#2610](https://github.com/Snipaste/feedback/issues/2610) [#2640](https://github.com/Snipaste/feedback/issues/2640) [#2659](https://github.com/Snipaste/feedback/issues/2659)


## v2.8.5
> 2023.05.08

* [专业版] 支持以 <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>C</kbd> 触发“复制图像为文件”
* [专业版] 优化超长自定义命令的显示
* [专业版] 支持全局快捷键禁用/启用触发角
* [专业版] 增加命令行选项 `barcode-decode FILE`
* 支持预设截图区域及命名 [#2541](https://github.com/Snipaste/feedback/issues/2541)
* 改善命令行调用的响应速度 [#2584](https://github.com/Snipaste/feedback/issues/2584)
* 截屏时长按 <kbd>Alt</kbd> 显示辅助线改成单击 <kbd>Alt</kbd> 切换显示
* 修复切换高对比度系统主题后托盘图标颜色不正确
* 其他问题修复或改进 [#2434](https://github.com/Snipaste/feedback/issues/2434) [#2548](https://github.com/Snipaste/feedback/issues/2548) [#2452](https://github.com/Snipaste/feedback/issues/2452) [#2473](https://github.com/Snipaste/feedback/issues/2473) [#2487](https://github.com/Snipaste/feedback/issues/2487) [#2498](https://github.com/Snipaste/feedback/issues/2498) [#2538](https://github.com/Snipaste/feedback/issues/2538)

## v2.8.4
> 2023.01.09

* 修复一个导致崩溃的错误

## v2.8.3 Beta2
> 2022.12.25

* [Mac] 修复无法取消启动项 (macOS 13) [#2428](https://github.com/Snipaste/feedback/issues/2428)

## v2.8.3
> 2022.12.18

* [专业版] 增加命令行选项: barcode-scan
* 优化标注工具栏在特定情况下出现的位置
* 其他问题修复 [#2414](https://github.com/Snipaste/feedback/issues/2414) [#2419](https://github.com/Snipaste/feedback/issues/2419)

## v2.8.2
> 2022.12.02

* [专业版] "刷新截图"增加快捷键触发: <kbd>F5</kbd>
* 修复鼠标光标图案绘制 [#2399](https://github.com/Snipaste/feedback/issues/2399)
* 修复读取部分字体导致崩溃的问题 [#2401](https://github.com/Snipaste/feedback/issues/2401)
* 修复读取部分剪贴板图像出现偏移的问题

## v2.8.1
> 2022.11.22

* 在截图时可多次按下 <kbd>Ctrl</kbd>+<kbd>A</kbd>，将选中当前屏幕/所有屏幕 [#2392](https://github.com/Snipaste/feedback/issues/2392)
* 修复"刷新截图"功能的一些问题
* 修复一个导致崩溃的错误
* 其他问题修复 [#2393](https://github.com/Snipaste/feedback/issues/2393)

## v2.8
> 2022.11.15

* [专业版] 支持识别二维码/条形码 [#23](https://github.com/Snipaste/feedback/issues/23)
* [专业版] 长按“刷新截图”可持续刷新截屏（松开时确定截屏）
* [专业版] 改善超级截图的可靠性 [#2242](https://github.com/Snipaste/feedback/issues/2242)
* [专业版] 贴图右键菜单可开/关显示标注
* [专业版] 新增选项：自由选中标注
* [专业版] 新增选项：可自定义贴图 Esc 和 Shift+Esc 的功能
* [专业版] 贴图支持 Windows 11 虚拟桌面 [#2233](https://github.com/Snipaste/feedback/issues/2233)
* [专业版] “复制为文件”功能会应用用户设置的文件命名规则
* [专业版] 增加命令行选项：
  * 白板支持指定背景色，支持仅显示在鼠标所在屏幕: `whiteboard --color black --active-screen`
  * 截图输出支持跳过自动保存: `snip -o no-auto-save`
  * 打开"管理贴图组"窗口: `show-group-manager`
* [Mac] 支持多屏截图 [#1026](https://github.com/Snipaste/feedback/issues/1026)
* [Mac] 启动项适配 macOS Ventura [#2366](https://github.com/Snipaste/feedback/issues/2366)
* [Mac] 修复贴图时显示了文件图标而非图像文件内容 [#2147](https://github.com/Snipaste/feedback/issues/2147)
* 升级到 Qt 6.2.4
* 右键菜单样式适配 Windows 11
* 新增选项：记住上一次使用的图片扩展名 [#2238](https://github.com/Snipaste/feedback/issues/2238)
* 新增[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE): `General/gpu_acceleration`
* 增加命令行选项：
  * 清空当前贴图组: `empty-group`
* 切换贴图组时，默认选中下一个贴图组 [#2354](https://github.com/Snipaste/feedback/issues/2354)
* 改善多屏多 DPI 的界面适配 [#2256](https://github.com/Snipaste/feedback/issues/2256)
* 当一次性请求过多贴图时，会弹框请求用户确认 [#2291](https://github.com/Snipaste/feedback/issues/2291)
* 在贴图上进行标注时，如果不小心隐藏了工具栏，此时会有右键菜单帮助用户退出标注或显示工具栏
* 设置全局快捷键时，如果设置失败，会提示失败原因
* 不再向配置文件 (config.ini) 中写入频繁变动的配置项 `Misc/preload_dlls_last_interval`
* 修复显示器断开连接后（如系统进入睡眠）可能导致的崩溃 [#2208](https://github.com/Snipaste/feedback/issues/2208)
* 修复远程桌面时无法在截图中输入'C' [#2078](https://github.com/Snipaste/feedback/issues/2078)
* 修复在全屏的火狐浏览器中无法进行元素检测
* 修复无法在 Waterfox 浏览器进行元素检测 [#2307](https://github.com/Snipaste/feedback/issues/2307)
* 修复截图后播放音效的卡顿问题
* 修复部分标注没有启用抗锯齿
* 修复贴图时部分文字被错误解析成颜色值 [#2197](https://github.com/Snipaste/feedback/issues/2197)
* 其他细节优化及问题修复 [#2175](https://github.com/Snipaste/feedback/issues/2175) [#2225](https://github.com/Snipaste/feedback/issues/2225) [#2243](https://github.com/Snipaste/feedback/issues/2243) [#2281](https://github.com/Snipaste/feedback/issues/2281)

## v2.7.3 Beta (Windows 桌面版)
> 2022.02.09

* 修复无法在 Windows 7 系统运行 [#2103](https://github.com/Snipaste/feedback/issues/2103)

## v2.7.3
> 2022.01.30

* 小问题修复

## v2.7.2
> 2022.01.26

* [专业版] 新功能：刷新截图 (同时保持已有标注不变) [#2077](https://github.com/Snipaste/feedback/issues/2077)
* [专业版] 带阴影/不带阴影截图 (通过 `--shadow` 或 `--no-shadow`) 的效果不再影响之后的截图
* 修复前台窗口的标题包含非法字符时快速保存失败 [#2068](https://github.com/Snipaste/feedback/issues/2068)
* 修复小键盘数字键不响应部分功能

## v2.7.1
> 2021.12.21

* 修复在 AMD 显卡上的崩溃问题 [#2056](https://github.com/Snipaste/feedback/issues/2056)
* 修复部分情况下屏幕 DPI 变更后托盘图标变模糊 [#512](https://github.com/Snipaste/feedback/issues/512)
* 部分用户界面动态适应屏幕 DPI 变更
* 当鼠标在贴图窗口内部时不触发窗口缩放 [#2047](https://github.com/Snipaste/feedback/issues/2047)
* [Mac] 修复部分用户在系统主题深色/浅色模式切换后图标颜色错误 [#2046](https://github.com/Snipaste/feedback/issues/2046)
* [Mac] 修复在扩展屏上回放截图历史出现错位 [#2034](https://github.com/Snipaste/feedback/issues/2034)

## v2.7
> 2021.11.25

* [专业版] 修复超级截屏未被纳入截屏历史 [#1998](https://github.com/Snipaste/feedback/issues/1998)
* [专业版] 修复临时隐藏的贴图被恢复后，贴图的层级关系出错
* [专业版] 修复"显示已隐藏贴图"使用了重复的助记键
* 修复从 Chrome 浏览器复制的图像贴图后显示为网址 [#2013](https://github.com/Snipaste/feedback/issues/2013)
* 修复部分情况下标注工具栏出现在错误的位置
* 防止取色器出现在截图中 [#1963](https://github.com/Snipaste/feedback/issues/1963)
* 改善画连续折线的识别率
* 改善 JPEG 格式图片的保存质量
* 其他细节优化及其他小问题修复 [#1961](https://github.com/Snipaste/feedback/issues/1961) [#2008](https://github.com/Snipaste/feedback/issues/2008)

## v2.6.6
> 2021.08.14

* [专业版] 暂时隐藏部分贴图后，可以继续新增需要暂时隐藏的贴图
* 修复检测窗口元素时可能导致的卡死
* 其他小问题修复 [#1944](https://github.com/Snipaste/feedback/issues/1944) [#1950](https://github.com/Snipaste/feedback/issues/1950)

## v2.6.6 Beta2
> 2021.08.14

* [Mac] 修复截图色差

## v2.6.6 Beta
> 2021.08.12

* [Mac] 修复多屏识别问题

## v2.6.5
> 2021.08.04

* 标注时可以按 <kbd>Tab</kbd> 切换 矩形/椭圆 或 直线/箭头
* [Mac] 修复一些问题

## v2.6.4
> 2021.08.01

* [Win] 修复在部分 Intel 集成显卡上的崩溃问题
* [Mac] 修复颜色对话框无法显示 [#1932](https://github.com/Snipaste/feedback/issues/1932)
* [Mac] 改善窗口元素检测与第三方程序（如 LyricsX）的兼容性 [#1932](https://github.com/Snipaste/feedback/issues/1932)

## v2.6
> 2021.07.28

* [专业版] 新增放大镜标注 [#193](https://github.com/Snipaste/feedback/issues/193)
* [专业版] 马克笔/马赛克/高斯模糊/橡皮擦 支持椭圆形状
* [专业版] 矩形/椭圆区域橡皮擦可旋转
* [专业版] 箭头标注增加更多样式
* [专业版] 自定义截图大小的菜单增加“添加到预设”命令
* [专业版] 支持按住 <kbd>Ctrl</kbd> 围绕中心点调整矩形/椭圆标注的大小
* [专业版] 增加选项：工具条显示 Windows 10 原生分享按钮 [#1875](https://github.com/Snipaste/feedback/issues/1875)
* [专业版] 增加选项：主题色跟随系统主题 [#1771](https://github.com/Snipaste/feedback/issues/1771)
* [专业版] 增加选项：对橡皮擦工具禁用二次编辑
* [Mac] 支持 M1 芯片
* 增加选项：贴图最大尺寸
* 合并箭头工具到折线工具
* 标注工具新快捷键： <kbd>T</kbd> 文字 <kbd>B</kbd> 画笔
* <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> 移动光标支持法语键盘布局
* 贴图窗口支持越南语输入法
* [Win] 按住 <kbd>Shift</kbd> 显示高级托盘菜单 [#1753](https://github.com/Snipaste/feedback/issues/1753)
* [Win] 修复版本升级后托盘图标会被自动隐藏
* [Mac] 按住 <kbd>Alt</kbd> 显示高级托盘菜单 [#1753](https://github.com/Snipaste/feedback/issues/1753)
* [Mac] 截图支持捕捉光标图案
* 重构界面元素检测，更加流畅、稳定及智能
* 大幅改善 马赛克/高斯模糊/文字 标注的绘制效率
* 其他问题修复及改进 [#1737](https://github.com/Snipaste/feedback/issues/1737) [#1758](https://github.com/Snipaste/feedback/issues/1758) [#1829](https://github.com/Snipaste/feedback/issues/1829) [#1831](https://github.com/Snipaste/feedback/issues/1831) [#1843](https://github.com/Snipaste/feedback/issues/1843) [#1890](https://github.com/Snipaste/feedback/issues/1890) [#1895](https://github.com/Snipaste/feedback/issues/1895) [#1916](https://github.com/Snipaste/feedback/issues/1916)

## v2.5.6
> 2020.12.10

* 改进标注的撤销/重做
* 其他细节优化及问题修复 [#1736](https://github.com/Snipaste/feedback/issues/1736)

## v2.5.5 Beta2 (Mac)
> 2020.12.02

* 修复特定情况下的菜单栏图标问题

## v2.5.5
> 2020.11.28

* 文件命名规则支持 `%title%` (活动窗口的标题) [#1589](https://github.com/Snipaste/feedback/issues/1589)
* 其他细节优化及问题修复 [#1685](https://github.com/Snipaste/feedback/issues/1685) [#1722](https://github.com/Snipaste/feedback/issues/1722) [#1727](https://github.com/Snipaste/feedback/issues/1727)

## v2.5.5 Beta (Mac)
> 2020.11.28

* 修复在 Big Sur 系统下的一些显示问题 [#1716](https://github.com/Snipaste/feedback/issues/1716)
* 其他细节优化及问题修复

## v2.5.4
> 2020.11.05

* 修复一个双屏问题 [#1698](https://github.com/Snipaste/feedback/issues/1698)

## v2.5.3
> 2020.11.02

* 小问题修复 [#1684](https://github.com/Snipaste/feedback/issues/1684) [#1685](https://github.com/Snipaste/feedback/issues/1685)

## v2.5.2
> 2020.10.18

* [专业版] 支持自定义标注工具条的调色板的颜色数量
* [专业版] 更灵活的序号标注
  * 第一个数字可以大于 1
  * 支持使用 <kbd>+</kbd>/<kbd>-</kbd> 按键调整序号
* 其他细节优化及问题修复（[#1644](https://github.com/Snipaste/feedback/issues/1644) 等）
* 增加意大利语翻译（感谢 [mausan66](mailto:muox66@gmail.com)）

## v2.5.2 Beta (Mac)
> 2020.10.18

* 修复模拟按键的问题 [#1664](https://github.com/Snipaste/feedback/issues/1664)
* 修复截图区域的尺寸计算 [#1659](https://github.com/Snipaste/feedback/issues/1659)
* 修复贴图没有出现在截图位置的问题
* 其他问题修复

## v2.5.1
> 2020.08.26

* 改用 px/dip 表示物理像素/逻辑像素 [#1008](https://github.com/Snipaste/feedback/issues/1008)
* 上次保存目录不存在时，使用其上一层目录 [#1632](https://github.com/Snipaste/feedback/issues/1632)
* 修复部分标注设置没有记住的问题 [#1635](https://github.com/Snipaste/feedback/issues/1635)
* 修复捕捉的光标的大小 [#1625](https://github.com/Snipaste/feedback/issues/1625)
* 其他问题修复

## v2.5
> 2020.08.12

* [专业版] 支持双向/反向箭头标注 [#843](https://github.com/Snipaste/feedback/issues/843)
* [专业版] 矩形/折线/箭头/画笔支持虚线、点线等样式 [#92](https://github.com/Snipaste/feedback/issues/92)
* [专业版] 支持带边框输出图像，并可以指定边框颜色和宽度 [#197](https://github.com/Snipaste/feedback/issues/197)
* [专业版] 带阴影输出图像支持指定阴影颜色和宽度
* [专业版] 支持自定义文字标注背景色 [#1529](https://github.com/Snipaste/feedback/issues/1529)
* [专业版] 可设置截屏区域大小的单位 (dp/px) [#1008](https://github.com/Snipaste/feedback/issues/1008)
* [专业版] 支持批量保存选中的贴图 [#1507](https://github.com/Snipaste/feedback/issues/1507)
* 增加截图操作提示（可在首选项中关闭）[#902](https://github.com/Snipaste/feedback/issues/902)
* 首选项窗口增加 `打开配置文件所在目录` 按钮
* 新增选项：忽略其他程序模拟的"复制"命令 [#1531](https://github.com/Snipaste/feedback/issues/1531)
* 新增[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：`Paste/include_active_window_for_multiselection` [#1461](https://github.com/Snipaste/feedback/issues/1461)
* 修复跨屏不同 DPI 可能导致的程序卡死 [#1489](https://github.com/Snipaste/feedback/issues/1489)
* 修复 Win7 下贴图出现在 Alt-Tab 界面中 [#1528](https://github.com/Snipaste/feedback/issues/1528)
* 修复贴图镜像翻转后，文字标注无法正常工作
* 贴图取色器显示局部坐标
* 新贴图继承当前截图的标注工具条设置
* 对截屏进行标注时，在截屏区域外右键单击可自动框选截屏区域
* 文本标注的默认字体使用当前设置的界面字体 [#1522](https://github.com/Snipaste/feedback/issues/1522)
* 改善标注工具条在不同 DPI 下的外观
* 大量细节改进

## v2.4
> 2020.05.20

* [专业版] 新增序号标注 [#227](https://github.com/Snipaste/feedback/issues/227)
* [专业版] 标注支持圆角矩形 [#1185](https://github.com/Snipaste/feedback/issues/1185)
* [专业版] 文字标注支持指定描边颜色及粗细 [#83](https://github.com/Snipaste/feedback/issues/83)
* [专业版] 文字标注支持背景色填充
* [专业版] 按住 <kbd>Ctrl</kbd> 再点击工具条上的 复制/贴图/保存 按钮，可获取带阴影图像
* [专业版] 在贴图上按 <kbd>Ctrl</kbd>+<kbd>A</kbd> 可以全选所有贴图
* [专业版] 默认启用"为不同标注形状分别记忆颜色"
* 文字标注支持从下拉框选择文字大小
* 文字标注支持滚轮调整文字大小
* 首选项中可以选择彩色托盘图标
* 除了鼠标滚轮和 <kbd>1</kbd> <kbd>2</kbd>，也可以使用 <kbd>[</kbd> <kbd>]</kbd> 调节画笔粗细
* 橡皮擦工具的快捷键调整为 <kbd>E</kbd>
* 首次双击关闭贴图时弹出提示 [#1514](https://github.com/Snipaste/feedback/issues/1514)
* 修复光标图案捕捉
* 其他问题修复

## v2.4 Beta (Mac)
> 2020.05.20

* 修复部分用户无法正常启动 App
* 修复全局快捷键与其他 App 冲突的问题
* "重做" 快捷键调整为 <kbd>Cmd</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>

## v2.3.5
> 2020.04.13

* 修复以管理员身份开机启动后没有获得管理员权限 [#1477](https://github.com/Snipaste/feedback/issues/1477)

## v2.3.5 Beta
> 2020.04.12

* 修复无法以管理员权限开机启动
* 修复中文字体描边 [#1475](https://github.com/Snipaste/feedback/issues/1475)
* [Mac] 修复首选项窗口在深色模式下的样式错误
* [Mac] 修复菜单栏图标的菜单点击无效的问题 [#1467](https://github.com/Snipaste/feedback/issues/1467)

## v2.3.4
> 2020.04.10

* [专业版] 修复贴图多选问题
* 其他问题修复

## v2.3.3
> 2020.04.08

* 修复免费版无法按住 <kbd>Shift</kbd> 画正方形/画圆 [#1462](https://github.com/Snipaste/feedback/issues/1462)
* 修复标注在半透明贴图上显示不正确的问题
* 修复选项"总是忽略 HTML 格式"的逻辑错误
* 修复贴图对部分 svg 文件渲染错误的问题
* 默认勾选"增强版托盘菜单"
* 改善标注的描边文字效果
* 其他问题修复（[#1461](https://github.com/Snipaste/feedback/issues/1461) 等）

## v2.3.3 Beta (Mac)
> 2020.04.07

* 降低触控板手势缩放贴图的敏感度
* 其他问题修复（[#1439](https://github.com/Snipaste/feedback/issues/1439)、[#1434](https://github.com/Snipaste/feedback/issues/1434) 等）

## v2.3.2
> 2020.03.20

* [专业版] 贴图支持反色操作（按键 <kbd>6</kbd>）
* [专业版] 当贴图背景使用了自定义颜色时，复制/保存时保留背景色
* [专业版] 支持以原始大小保存已缩放的贴图 [#1430](https://github.com/Snipaste/feedback/issues/1430)
* [微软商店版] 修复部分用户无法内购的问题
* 修复截图时 <kbd>Ctrl</kbd> + <kbd>P</kbd> 无法打印截图
* 其他小问题修复

## v2.3.1
> 2020.02.27

* [专业版] 允许对透明白板模式启用/禁用鼠标穿透
* [专业版] 修复透明白板模式下橡皮擦工具不起效 [#1421](https://github.com/Snipaste/feedback/issues/1421)
* [专业版] 修复使用快捷键保存截图没有带上阴影 [#1413](https://github.com/Snipaste/feedback/issues/1413)
* [专业版] 修复裁剪贴图的透明度问题 [#1420](https://github.com/Snipaste/feedback/issues/1420)
* 新增[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：`Paste/disable_frame_resize`
* 其他问题修复

## v2.3.1 Beta (Mac)
> 2020.02.27

* 修复当使用外接鼠标的滚轮调整画笔粗细时，光标形状没有实时更新 [#1310](https://github.com/Snipaste/feedback/issues/1310)
* 其他问题修复

## v2.3
> 2020.02.20

* [专业版] 标注工具支持中心点缩放（按住 Ctrl）以及等比例缩放（按住 Shift）
* [专业版] [命令行选项](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9)支持指定多个输出方式，可支持将截图发送到其他程序（如 OCR 程序）
* [专业版] 增加选项：在保存文件时隐藏截图窗口/贴图窗口
  * 以便利用 Listary 跳转到指定文件夹
* [专业版] 超级截图支持绑定到 "截图、复制并贴图"
* [专业版] 触发角支持设置程序黑名单
* [专业版] 修复触发角错误触发的问题 [#1398](https://github.com/Snipaste/feedback/issues/1398)
* 标注工具支持对移动、旋转等操作的撤销/重做
* 支持 Ctrl+W 关闭贴图窗口 [#1384](https://github.com/Snipaste/feedback/issues/1384)
* 贴图支持读取 apng 格式的动画文件 [#1064](https://github.com/Snipaste/feedback/issues/1064)
* 新增[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：`Paste/magnetic_attach_threshold` [#1386](https://github.com/Snipaste/feedback/issues/1386)
* 其他问题修复 [#1369](https://github.com/Snipaste/feedback/issues/1369)

## v2.3 Beta (Mac)
> 2020.02.20

* 解决 macOS 10.15 的安全权限申请相关问题
* 修复自定义截图窗口没有自动获取焦点 [#1382](https://github.com/Snipaste/feedback/issues/1382)

## v2.2.8
> 2019.12.13

* 修复一个崩溃问题 [#1350](https://github.com/Snipaste/feedback/issues/1350)

## v2.2.7
> 2019.12.02

* 恢复显示所有贴图时不改变原窗口焦点 [#1337](https://github.com/Snipaste/feedback/issues/1337)
* 修复二次编辑时工具栏的按钮状态不匹配
* 修复特定系统设置下无法正确以管理员身份启动

## v2.2.6
> 2019.11.20

* [专业版] 多选贴图后支持所有贴图操作
* 命令行选项支持指定文件名输出 `-o FILE_NAME` [#1321](https://github.com/Snipaste/feedback/issues/1321)
* 修复任务计划被禁用后无法启动 Snipaste
* 其他问题修复

## v2.2.5
> 2019.10.28

* [专业版] 修复多选贴图进行批量缩放的一些问题 [#1304](https://github.com/Snipaste/feedback/issues/1304)

## v2.2.5 Beta (Mac)
> 2019.10.28

* 修复在截屏窗口/贴图窗口使用 Cmd+C/Cmd+V 快捷键没反应

## v2.2.4
> 2019.10.24

* [专业版] 支持多选贴图进行批量缩放
* 改善对高分屏的支持
  - 在多屏不同 DPI 的环境下，标注工具栏等界面会保持统一的视觉大小
* 修复文字转贴图的字体颜色 [#1281](https://github.com/Snipaste/feedback/issues/1281)
* 修复选项"固定到所有虚拟桌面"重启后失效 [#1288](https://github.com/Snipaste/feedback/issues/1288)
* 修复贴图超出屏幕范围时无法显示标注工具栏 [#1291](https://github.com/Snipaste/feedback/issues/1291)
* 新增[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE): `Hotkey2/allow_simple_hotkeys` [#1277](https://github.com/Snipaste/feedback/issues/1277)
* 提高以命令行方式调用的成功率

## v2.2.4 Beta (Mac)
> 2019.10.24

* 修复在保存文件时无法使用 剪切/复制/粘贴/全选 快捷键
* 修复命令行选项 `show-tray-menu` 不起作用

## v2.2.3 Beta2 (Mac)
> 2019.10.11

* 修复在 macOS Catalina 系统上的输入法问题 [#1280](https://github.com/Snipaste/feedback/issues/1280)

## v2.2.3
> 2019.10.06

* [专业版] 支持以固定长宽比截图
* [专业版] 修复启动之后首次超级截图会崩溃 [#1260](https://github.com/Snipaste/feedback/issues/1260)
* [专业版] 修复贴图管理窗口关闭后无法再次显示的问题
* 修复截图时保存文件对话框无法用 <kbd>Esc</kbd> 取消保存 [#1257](https://github.com/Snipaste/feedback/issues/1257)
* 修复使用韩语输入法时文字标注吃字的问题
* 改善对高对比度模式的支持
* 防止一些简单按键被设为全局快捷键
* 其他一些小改进

## v2.2.3 Beta (Mac)
> 2019.10.06

* 增加可配置项：在所有桌面固定贴图
* 修复 Keynote 播放时进行截图没有获取到输入焦点的问题 [#1244](https://github.com/Snipaste/feedback/issues/1244)
* 修复 App 退出后剪贴板里的截图无法被读取的问题

## v2.2.2
> 2019.09.18

* [专业版] 支持贴图固定到指定/所有虚拟桌面
* [微软商店版] 修复部分用户无法勾选"以管理员身份"开机启动
* 新增命令行选项: create-group
* 内置快捷键响应支持非英式键盘布局
* 改善对跨 DPI 移动窗口的支持
* 改善白板模式及透明白板模式
* 支持检测未开启无障碍功能的 Chrome 的网页区域
* 修复特定 HTML 转贴图时会发生卡顿
* 新增韩语翻译（感谢 [박경태](https://github.com/parkkyeongtae)）
* 新增捷克语翻译（感谢 Štěpán Hašler）

## v2.2 Beta2 (Mac)
> 2019.08.03

* 修复内存泄漏
* 修复部分控件的背景色（在早于 macOS Mojave 的系统上）
* 滑动手势不再做特殊处理，遵从贴图操作的选项设置 [#1193](https://github.com/Snipaste/feedback/issues/1193)
* UI 微调

## v2.2.1 Beta2
> 2019.07.05

### 修复：
* [桌面版] 开启`以管理员身份开机启动`后被 Windows Defender 误报 [#1153](https://github.com/Snipaste/feedback/issues/1153)

## v2.2.1
> 2019.07.04

### 修复：
* 在 Windows 10 (1903) 之前的 Windows 10 操作系统上，通知栏图标的默认颜色不正确

## v2.2
> 2019.07.02

### 新增:
* 适配 Windows 10 浅色任务栏
* 支持使用 Ctrl+1/2/3 切换标注形状 [#664](https://github.com/Snipaste/feedback/issues/664)
* 贴图右键菜单新增:
  * 图像处理（旋转/镜像）
  * 复制原始大小图像 [#1025](https://github.com/Snipaste/feedback/issues/1025)
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：
  * Output/auto_alpha_channel_removal [#1077](https://github.com/Snipaste/feedback/issues/1077)
  * Annotate/auto_select_on_creation [#1126](https://github.com/Snipaste/feedback/issues/1126)

### 修复：
* [UWP] 以管理员身份启动时无法进行购买 [#1059](https://github.com/Snipaste/feedback/issues/1059)
* [#833](https://github.com/Snipaste/feedback/issues/833) [#910](https://github.com/Snipaste/feedback/issues/910) [#981](https://github.com/Snipaste/feedback/issues/981) [#1045](https://github.com/Snipaste/feedback/issues/1045) [#1050](https://github.com/Snipaste/feedback/issues/1050) [#1080](https://github.com/Snipaste/feedback/issues/1080) [#1086](https://github.com/Snipaste/feedback/issues/1086) [#1098](https://github.com/Snipaste/feedback/issues/1098) [#1099](https://github.com/Snipaste/feedback/issues/1099) [#1103](https://github.com/Snipaste/feedback/issues/1103) [#1105](https://github.com/Snipaste/feedback/issues/1105)

### 改进：
* 改善描边文字绘制效果
* 按住 <kbd>Shift</kbd> 再将图像拖拽到贴图，可保留原贴图


### 专业版新增特性

* 支持带阴影复制/保存图像
* 更多箭头样式
* 缩略图模式下可以进行标注
* 自动保存/快速保存的路径支持使用变量（从而截图可以按日期放在不同文件夹里）
* 大部分标注工具支持旋转
* 矩形橡皮擦工具支持二次选中/编辑
* 超级截图支持自定义按键
* 超级截图支持触发普通截图（Windows） [#1085](https://github.com/Snipaste/feedback/issues/1085)
* 支持设置截屏的默认标注
* 增加更多命令行选项（可以绑定到全局快捷键）：
   * pick-color [#1096](https://github.com/Snipaste/feedback/issues/1096)
   * transparent-whiteboard [#1155](https://github.com/Snipaste/feedback/issues/1155)
   * paste-as-image
   * paste-as-plain-text
* 在贴图上使用右键拖拽进行缩略图时，按下左键可取消拖拽
* 改进全局快捷键的黑名单机制

## v2.2 Beta (Mac)
> 2019.07.02

* 暂不支持解锁专业版
* 适配深色模式
* 贴图支持触控板手势（捏合/旋转/双指滑动/双指双击）
* 支持命令行调用
* 支持拖拽贴图边缘进行缩放
* 贴图会在所有桌面显示
* 降低触控板双指滑动手势的敏感度 [#951](https://github.com/Snipaste/feedback/issues/951)
* 修复中文输入法问题
* 修复复制/保存图像的格式、大小以及色彩配置 [#836](https://github.com/Snipaste/feedback/issues/836) [#881](https://github.com/Snipaste/feedback/issues/881) [#946](https://github.com/Snipaste/feedback/issues/946) [#959](https://github.com/Snipaste/feedback/issues/959) [#971](https://github.com/Snipaste/feedback/issues/971)
* 修复窗口层级问题
* 修复从剪贴板/文件读取图像时的 DPI 设置 [#1031](https://github.com/Snipaste/feedback/issues/1031)
* 修复偶尔高 CPU 占用 [#787](https://github.com/Snipaste/feedback/issues/787)
* 兼容欧陆词典 [#1012](https://github.com/Snipaste/feedback/issues/1012)

## v2.1.4
> 2018.12.03

### 修复：
* 在一些边界条件下，进行标注时光标形状与鼠标行为不一致
* 特定条件下会启动失败

## v2.1.3
> 2018.11.12

### 修复：
* 标注工具的一些问题（感谢 StarryVoid）
* 在"简体-香港"区域格式下会生成错误的日期格式 [#969](https://github.com/Snipaste/feedback/issues/969)

### 改进：
* 画笔工具绘制的曲线更加平滑
* 改善自由绘制工具（包括画笔、橡皮擦以及模糊工具）的绘制效率
* 贴图支持响应菜单键

## v2.1.2
> 2018.10.18

* 紧急修复勾选"以管理员权限开机启动"后无法启动

## v2.1.1
> 2018.10.18

### 新增：
* **支持以管理员权限开机启动**
* 可自定义截屏时鼠标左键双击和中键单击的功能
* [专业版] 支持对多选的贴图批量设置其窗口阴影/置顶状态

### 改进：
* 一些细节优化

## v2.1
> 2018.10.01

### 新增：
* [专业版] 支持旋转矩形/椭圆形标注
* [专业版] 支持自定义贴图尺寸/缩放
* [专业版] 支持自定义贴图背景色 [#953](https://github.com/Snipaste/feedback/issues/953)
* [专业版] 更多预设命令示例
* [专业版] [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE): Misc/auto_download_max_image_size_mb
    * 用于限制自动下载的最大图片大小
* 文字标注支持斜体

### 改进：
* [专业版] 按住 Ctrl 再拖拽标注可以复制它
* [专业版] 当没有勾选“自动检测界面元素”时，也可以通过滑动滚轮进入元素框选模式
* [专业版] 改善多选贴图的批量移动，保证所有贴图最后具有相同的位移
* [专业版] 将网页图片拖拽到贴图上时，支持自动下载所有以 http/https 开头的图片链接（过去只支持带图像文件后缀的链接）
* [专业版] 支持在 'external' 命令中使用环境变量
* [专业版] 默认不启用"复制为文件"
* 为避免与复制/粘贴图像快捷键的冲突，对于选中标注的复制/粘贴快捷键改为 Ctrl-Shift-C / Ctrl-Shift-V
* 允许字体更小的文字标注
* 双击 Esc 键可以退出白板模式
* 纯文本转贴图时，自动隐藏首尾的空白字符（不影响文字源）

### 修复：
* [专业版] 无法使用方向键移动多选的贴图
* 当开始菜单激活时，特定条件下无法正常截屏
* 开始移动标注时，标注位置可能发生跳变
* 当网页正在全屏播放视频时进行截屏，然后按下 Esc 退出截屏，会导致视频退出全屏
* 又一个潜在的导致程序崩溃的问题

## v2.0.1
> 2018.07.25

### 新增：
* 西班牙语（墨西哥）翻译（感谢 [enriquedfa](https://github.com/enriquedfa)）

### 修复：
* 禁用触发角功能后超级截图失效
* 超级截图无法截取开始菜单
* 多选贴图后，改变贴图大小会触发其他贴图移动
* 复制已暂停的 GIF 贴图获得的仍然是动图
* 其他一些小问题

### 改进：
* 异步处理自动保存与快速保存，避免卡顿

## v2.0
> 2018.07.18

### 新增：
* **Snipaste 2.0 专业版**已经可以从[微软商店版](https://www.microsoft.com/store/apps/9P1WXPKB68KX)**内购**获得！（[专业版功能介绍](https://github.com/Snipaste/feedback/wiki/%E4%B8%93%E4%B8%9A%E7%89%88)）
* **注意**：Snipaste 2.x 版本仅对个人用户免费，如需在公司内使用请购买专业版或使用 Snipaste 1.x 版本
* 选项: 保持程序快速响应 [#498](https://github.com/Snipaste/feedback/issues/498)
* 升级延迟截图对话框为自定义截图对话框，可以指定截图位置或尺寸

### 修复：
* 导致崩溃的一些问题
* 以管理员权限开机启动后进程的 IO 优先级太低导致程序响应慢 [#498](https://github.com/Snipaste/feedback/issues/498)
* 以 Window Bitmap 格式复制图像时未正确设置其 DPI 缩放值 [#811](https://github.com/Snipaste/feedback/issues/811)
* 用于贴图缩放/透明度调整的快捷键没效果
* 其他一些问题：[#829](https://github.com/Snipaste/feedback/issues/829) [#839](https://github.com/Snipaste/feedback/issues/839) [#869](https://github.com/Snipaste/feedback/issues/869) [#931](https://github.com/Snipaste/feedback/issues/931)

### 改进：
* 调整了一些命令行接口
* 移除了一些不必要的[高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)
* 避免对失去响应的窗口进行界面元素检测
* 两次重置贴图窗口会恢复到前一次的缩放级别 [#323](https://github.com/Snipaste/feedback/issues/323)
* 当贴图上有唯一的文字标注时，可以从右键菜单复制其文字内容
* 当文字标注颜色为半透明时禁用描边的阴影
* 允许禁用贴图的缩放/调整透明度操作
* 微调箭头样式

## v1.16.2
> 2018.01.22

> 这是最后一个支持 XP 及 Vista 的版本

### 修复：
* 切换配置文件时程序崩溃
* （微软商店）用户移动应用到其他驱动器后应用无法再启动

## v1.16.2 Beta for Mac
> 2018.01.20

### 修复：
* 无法输入中文/无法显示输入法选框 [#686](https://github.com/Snipaste/feedback/issues/686)
* 菜单栏及程序坞均隐藏后无法正常截图 [#687](https://github.com/Snipaste/feedback/issues/687)
* 贴图右键菜单不能及时弹出的问题
* 同步 Windows 版更新（v1.16, v1.16.1）

## v1.16.1
> 2018.01.17

### 修复：
* 截图时马赛克/高斯模糊失效 [#789](https://github.com/Snipaste/feedback/issues/789)
* Wi-Fi 扫描问题 [#788](https://github.com/Snipaste/feedback/issues/788) [QTBUG-40332](https://bugreports.qt.io/browse/QTBUG-40332)

## v1.16
> 2018.01.16

### 新增：
* 选项：自定义调整贴图缩放、透明度的快捷键
* 选项：完全禁用光标捕捉（可改善截图的响应速度）
* 选项：禁用快速保存的弹出通知 [#636](https://github.com/Snipaste/feedback/issues/636)
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Misc/squeeze_memory
  * 启用可降低内存消耗，但同时会降低截图响应速度
* 截图时按下 <kbd>V</kbd> 可复制图片尺寸 [#633](https://github.com/Snipaste/feedback/issues/633)
* 贴图右键菜单增加 33.3% 缩放
* 支持响应非拉丁语键盘的 <kbd>Ctrl</kbd> + <kbd>C</kbd> [#689](https://github.com/Snipaste/feedback/issues/689)
* 支持以 <kbd>Ctrl</kbd> + <kbd>Insert</kbd> 复制截图/贴图
* [全新的命令行选项](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9)
* 塞尔维亚语翻译（感谢 [ozzii.translate](mailto:ozzii.translate@gmail.com)）

### 修复：
* 文字输入残留光标的问题
* 预览截图声音时会额外打开音乐播放器的问题 [#647](https://github.com/Snipaste/feedback/issues/647)
* 日志在跟踪/调试模式下没有即时更新

### 改进：
* 颜色卡片贴图的缩略图使用相应颜色边框 [#630](https://github.com/Snipaste/feedback/issues/630)
* 描边的文字标注现已加上阴影

## v1.16 Beta2 for Mac
> 2017.12.16

### 修复：
* 出现 "Do not localize" 窗口的问题

## v1.16 Beta for Mac
> 2017.12.09

* 首次公开发布

## v1.15.3
> 2017.11.12

### 注意：
* 该版本的微软商店版会导致 Snipaste 配置丢失，如需恢复，请先关闭 Snipaste，然后将

  `%LOCALAPPDATA%\Packages\45479liulios.17062D84F7C46_p7pnf6hceqser\LocalCache\Local\snipaste.com\Snipaste`

  中的所有文件复制到

  `%LOCALAPPDATA%\Packages\45479liulios.17062D84F7C46_p7pnf6hceqser\LocalState`

  再启动 Snipaste 即可。抱歉……

### 新增：
* （微软商店版）增加日志记录选项

### 修复：
* （微软商店版）程序退出后发生崩溃并自动重启
* （微软商店版）无法从选项窗口打开配置文件
* 部分标注在贴图窗口缩放/旋转后无法被正确移动

### 改进：
* 支持在文件/文件夹路径中使用 `$` 和 `%` 符号 [#626](https://github.com/Snipaste/feedback/issues/626)
* 其它一些小改进，如 [#621](https://github.com/Snipaste/feedback/issues/621)

## v1.15.2
> 2017.11.06

### 新增：
* （微软商店版）选项窗口增加开机启动开关
  * Windows 秋季创意者更新后（1709），如果关机时 Snipaste 正在运行，系统会在下次开机时自动启动 Snipaste，不管该选项是否被选中。如果这导致托盘区出现了两个 Snipaste 图标，可在选项窗口禁用开机启动（临时解决方案）
* 按住 <kbd>Shift</kbd> 可限制截图区域或标注的移动方向为水平/垂直
  * 文本标注暂不支持此操作
* 选项：禁用截屏时使用滚轮调整画笔大小的功能
* 预告：Mac 版即将公测 😉

### 修复：
* 少部分用户笔记本电源断开后 Snipaste 被系统关闭 [#522](https://github.com/Snipaste/feedback/issues/522)
* 过时的文档说明

### 改进：
* 改善马克笔的高亮效果 [#580](https://github.com/Snipaste/feedback/issues/580) [#596](https://github.com/Snipaste/feedback/issues/596) [#616](https://github.com/Snipaste/feedback/issues/616)
* 改善对文字进行二次编辑的交互 [#590](https://github.com/Snipaste/feedback/issues/590)
* `history_dir` 支持使用环境变量
* 其它一些小改进

## v1.15.1
> 2017.09.27

### 修复：
* 由于精简 `config.ini` 导致的一些错误（文件命名规则、快速保存等）

## v1.15
> 2017.09.26

### 新增：
* Snipaste 已上架 [微软商店](https://www.microsoft.com/store/apps/9P1WXPKB68KX) 🎉
* （微软商店版）新增命令行调用支持
  * 可直接使用 `snipaste.exe` 后接命令行参数的形式进行调用（无需输入 Snipaste 的安装路径）
* 截屏时按住 <kbd>Shift</kbd> 拖动鼠标可进行正方形截图
* 再次调整了鼠标穿透的快捷键功能：
  * 取消了双击 <kbd>X</kbd> 这一触发方式
  * 为防止误触，默认不绑定快捷键，如果你了解并需要这项功能，请到 `选项窗口` - `快捷键` - `切换鼠标穿透` 为其设置快捷键
  * 请务必点击旁边 `i` 图标了解其用法
* 支持在选项窗口中设置配置文件的存储路径
  * 未进行充分测试，如有问题欢迎反馈
* 新增一些命令行参数，支持设置[截图的输出方式](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9#%E6%88%AA%E5%9B%BE%E8%BE%93%E5%87%BA%E9%80%89%E9%A1%B9)
  * 未进行充分测试，如有问题欢迎反馈
* 土耳其语翻译（感谢 [thrylmz](https://github.com/thrylmz)）

### 修复：
* （微软商店版）更新后未自动重启
* 矩形标注位置偏移问题
* 窗口检测与任务栏区域的一些问题 [#584](https://github.com/Snipaste/feedback/issues/584)
* `自动备份`关闭后切换贴图组的快捷键仍然占用的问题 [#499](https://github.com/Snipaste/feedback/issues/499)
* 点击小红点可能导致崩溃的问题
* 与贴图组相关的一些崩溃 bug

### 改进：
* 默认文字标注的样式改为不描边的
* 当处于马赛克/高斯模糊工具的矩形填充模式时，可以使用 <kbd>鼠标滚轮</kbd> 调整模糊级别
* 改善了标注时光标的可见性 [#544](https://github.com/Snipaste/feedback/issues/544)
* 正在文字标注时按下 <kbd>Esc</kbd> 改为结束文字编辑，而非退出截图 [#562](https://github.com/Snipaste/feedback/issues/562)
* 切换贴图组时不自动选中下一贴图组 [#556](https://github.com/Snipaste/feedback/issues/556)
* 精简 `config.ini`，只保留与默认值不同的选项

## v1.14
> 2017.08.04

### 新增：
* 一些操作提示（小红点）
* 支持对 `config.ini` 使用软链接（symbolic link）[#494](https://github.com/Snipaste/feedback/issues/494)
* 截图时按下 <kbd>Alt</kbd> 键增加显示两条中央辅助线 [#513](https://github.com/Snipaste/feedback/issues/513)
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Misc/reload_image_plugins_timer
  * Snipaste 闲置一段时间后启动截图出现卡顿的同学可以试试
* 俄语翻译（感谢 [vanja-san](https://github.com/vanja-san)）
* 阿拉伯语翻译（感谢 MFMDawdeh）

### 修复：
* 对文件管理器的界面元素识别
* 在机械硬盘上，Snipaste 闲置一段时间后，再次移动贴图可能会卡顿
* 一些导致程序崩溃的问题及其他许多小问题

### 改进：
* 屏幕 DPI 变更时自动更新托盘图标 [#512](https://github.com/Snipaste/feedback/issues/512)
* 调整了部分选项的默认值

## v1.13.6
> 2017.06.09

### 修复：
* 某些多屏配置下截图时无法进行标注 [#487](https://github.com/Snipaste/feedback/issues/487)

## v1.13.5
> 2017.06.08

### 改进：
* 合并了工具条上的矩形和椭圆标注
* 马赛克和高斯模糊的模糊程度可以独立调节了
* 贴图恢复失败时避免清理记录文件

### 修复：
* 截图时自动跳出的问题 [#485](https://github.com/Snipaste/feedback/issues/485)
* 标注灰度图像后复制或保存，色彩丢失
* 贴图的提示文本闪烁的问题

## v1.13.4
> 2017.06.06

### 新增：
* 托盘菜单项：禁用快捷键 [#171](https://github.com/Snipaste/feedback/issues/171)

### 修复：
* 截图窗口 DPI 问题 [#481](https://github.com/Snipaste/feedback/issues/481) [#482](https://github.com/Snipaste/feedback/issues/482)
* 环境变量冲突问题 [#457](https://github.com/Snipaste/feedback/issues/457) [#480](https://github.com/Snipaste/feedback/issues/480)
* 截图时按 Tab 键无法切换元素检测/窗口检测 [#479](https://github.com/Snipaste/feedback/issues/479)

### 改进：
* 截图后由于自动保存截图记录造成的卡顿

## v1.13.3
> 2017.06.02

### 新增：
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：增加 process_priority，移除 high_process_priority
  * 默认进程优先级为“高于正常”

### 修复：
* 贴图上马赛克/高斯模糊的绘制有误
* 贴图旋转后使用方向键控制对标注进行移动时方向不正确
* 其他一些小问题

### 改进：
* 更新至 Qt 5.9
* 其他许多小改进

## v1.13.2
> 2017.05.20

### 新增：
* 使用方向键移动标注图案
* 崩溃信息记录
* 新日志等级：跟踪
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Paste/force_html_wrap
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Misc/uppercase_hex_color

### 修复：
* 一些导致程序崩溃的问题及其他许多小问题

## v1.13.1
> 2017.05.07

### 修复：
* 帮助窗口在特定条件下不可见
* 锚点在特定条件下出现绘制残影
* 光标悬停在文字框的状态 [#442](https://github.com/Snipaste/feedback/issues/442)

### 改进：
* 更新部分翻译

## v1.13
> 2017.05.05

### 新增：
* **连续调整画笔粗细：**<kbd>鼠标滚轮</kbd> 或 <kbd>1</kbd> / <kbd>2</kbd> [#276](https://github.com/Snipaste/feedback/issues/276)
* 新的贴图组切换方式 [#390](https://github.com/Snipaste/feedback/issues/390)
* 更方便的文字旋转方式
  * **同时移除了 <kbd>Ctrl</kbd> + <kbd>鼠标拖拽</kbd> 进行文字旋转**
* 进行文字标注时，使用 <kbd>鼠标中键</kbd> 进行粘贴 [#410](https://github.com/Snipaste/feedback/issues/410)
* 全局快捷键：截图并自动复制（默认 <kbd>Ctrl</kbd> + <kbd>F1</kbd>）
* 选项：显示截图边框锚点 [#391](https://github.com/Snipaste/feedback/issues/391)
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Snip/anchors_threshold
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Hotkey2/exchange_wasd_and_arrow_keys [#417](https://github.com/Snipaste/feedback/issues/417)
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Misc/path_simplify_tolerance [#371](https://github.com/Snipaste/feedback/issues/371)
* [命令行选项](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9)：`--switch` 更名为 `--switch-to`
* [命令行选项](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9)：新增 `--at` 用以指定贴图位置

### 改进：
* 性能提升
* 更新了部分图标、光标图案
* **为避免误触，将贴图设为鼠标穿透状态的快捷键由 单击 <kbd>X</kbd> 更改为 双击 <kbd>X</kbd>**
* 通过滑块的形状区分当前的马赛克/高斯模糊状态
* 合并所有帮助文档到一个窗口
* 令贴图阴影区域及缩略图边框可被截图探测到
* **HSV, HSL, HSI 中 S, V, L, I 的范围从 [0, 255] 更改为 [0, 100]** [#418](https://github.com/Snipaste/feedback/issues/418)
* 高清屏缩放问题（部分用户） [#372](https://github.com/Snipaste/feedback/issues/372)
* 自动取消因双击而被添加的标注 [#383](https://github.com/Snipaste/feedback/issues/383)
* 缩略图模式允许调整透明度 [#425](https://github.com/Snipaste/feedback/issues/425)
* 对缩略图边框应用透明度 [#425](https://github.com/Snipaste/feedback/issues/425)
* 在窗口选取状态（刚进入截图），使用 <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> 或 方向键 都能移动光标
* 允许在编辑状态使用快捷键对截屏区域进行调整
* 截图时禁止响应其他全局快捷键
* 白板模式禁用 <kbd>Esc</kbd> 键退出截图
* 检测程序双开时考虑符号链接

### 修复：
* 若干个崩溃 bug
* HTML 转图片在某些情况下格式出错 [#366](https://github.com/Snipaste/feedback/issues/403) [#403](https://github.com/Snipaste/feedback/issues/403)
* 当小娜活动时开始截图，截图窗口位于任务栏之下
* 特定双屏设置下工具条遮挡截图区域的问题
* 已缩放的贴图在重启恢复后可能尺寸不对
* 其他一些小 bug [#389](https://github.com/Snipaste/feedback/issues/389)

## v1.12
> 2017.03.09

### 新增：
* **支持将单个贴图设为鼠标穿透（激活贴图后按字母键 <kbd>X</kbd>）**
  * 同时不再支持将**所有**贴图设为鼠标穿透
  * **原全局快捷键（默认为 <kbd>F4</kbd>）的功能更改为 `取消光标所在位置的贴图的鼠标穿透状态；如果没有贴图位于光标之下，则取消所有贴图的鼠标穿透状态`**
  * 相应地，移除了命令行选项 `--click-through-toggle`，`--click-through-enable` 及 `--click-through-disable`，增加 `--no-click-through`
* 选项：自动保存所有截图到指定目录
* 选项：文字转图片的页边距 [#359](https://github.com/Snipaste/feedback/issues/359)
* 选项：日志级别
  * 不包括 XP 版
* [命令行选项](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9)：--config
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Update/fake_version
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：Log/file, Log/flush_interval, Log/rotate, Log/rotate_size_mb
* 法语翻译（感谢 **rico-sos**）

### 修复：
* 选项“截图时如有其他程序窗口被激活，自动退出截图”取消无效
* 部分标注形状的交互问题 [#351](https://github.com/Snipaste/feedback/issues/351)
* 当 Snipaste 被置于 Dropbox 同步目录时，可能导致软件自动更新失败 [#333](https://github.com/Snipaste/feedback/issues/333)
* 贴图翻转没有影响到标注
* HTML 转图片时空行间距过大 [#366](https://github.com/Snipaste/feedback/issues/366)
* HTML 转图片时可能未使用正确字体
* 特定条件下光标设置有误 [#363](https://github.com/Snipaste/feedback/issues/363)

### 改进：
* 提高默认配置下的 JPG 格式图像的质量 [#311](https://github.com/Snipaste/feedback/issues/311)
* 截图时支持响应贴图的全局快捷键
* 存储路径（快速保存/自动保存）支持环境变量 [#367](https://github.com/Snipaste/feedback/issues/367)
* 更新部分标注的光标图案，以体现画笔粗细 [#352](https://github.com/Snipaste/feedback/issues/352)
* 按住 <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> 不放进行光标移动时，自动加速
* 贴图窗口支持 <kbd>Ctrl</kbd> + <kbd>V</kbd> 快捷键
* 选项窗口新增多个提示图标
* 增强版托盘菜单中增加“重新启动”
* 更高效的图像高斯模糊
* 若干界面细节改进
* Qt 版本更新到 5.8
  * 不包括 XP 版

## v1.11.3
> 2017.01.27

### 新增：
* [常见问题](https://github.com/Snipaste/feedback/wiki/%E5%B8%B8%E8%A7%81%E9%97%AE%E9%A2%98)
* 越南语翻译（感谢 **evildeepblue**）

### 修复：
* 禁用自动检测窗口后，截图界面绘制不正常 [#317](https://github.com/Snipaste/feedback/issues/317)
* 一些细节问题

## v1.11.2
> 2017.01.17

### 修复：
* 开机自启检查到更新时，可能无法弹出下载更新的窗口 [#298](https://github.com/Snipaste/feedback/issues/298)

## v1.11.1
> 2017.01.15

### 新增：
* 选项：最大历史截图区域数
* 选项：循环切换历史截图区域
* 葡萄牙语（葡萄牙）翻译（感谢 **Luis Neves**）

### 修复：
* 以管理员身份自启时，更新版本时可能自动重启失败（感谢 **Fubuki**）
* 启动后第一次检查更新时短暂卡顿
* 切换界面语言后，选项窗口中部分控件的文字没有被更新
* 翻转贴图时在边界出现问题 [#248](https://github.com/Snipaste/feedback/issues/248)

### 改进：
* 优化启动后第一次截图的响应速度
* 优化按住 <kbd>鼠标左键</kbd> 并用 <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> 移动贴图的响应速度 [#315](https://github.com/Snipaste/feedback/issues/315)
  * 注意：该操作在 Windows XP 系统有问题
* 全面汉化所有对话框
* 多处细节优化 :)

## v1.11
> 2017.01.01

### 新增：
* 记忆多个历史截图区域 [#249](https://github.com/Snipaste/feedback/issues/249)
  * 截图时通过多次按下 <kbd>R</kbd> 或 <kbd>Shift</kbd> + <kbd>R</kbd> 来选择
  * 数量等于截图的最大历史记录数
* 增加一个备用更新源
* 在更新器对话框增加“重新检测”按钮
* [命令行选项](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9)：`--snip-whiteboard` 白板模式
* 选项：自定义截图时回车键的功能
* 选项：以鼠标指针为中心缩放贴图 [#277](https://github.com/Snipaste/feedback/issues/277)
* 选项：重置贴图操作对缩略图也起作用 [#280](https://github.com/Snipaste/feedback/issues/280)
* 选项：贴图上的拖放操作可保留原贴图
* 荷兰语翻译（感谢 **Stephan Paternotte**）

### 修复：
* 对 Nsight 控件进行元素检测导致其崩溃 [#290](https://github.com/Snipaste/feedback/issues/290)
* 特定条件下，截图结束后未解除对 <kbd>Win</kbd> 的屏蔽 [#272](https://github.com/Snipaste/feedback/issues/272)
* 启动后第一次在截图时按下 <kbd>Tab</kbd> 没有效果
* 检测更新时的一些逻辑错误

### 改进：
* 即使在编辑状态，也可以通过空格键隐藏/显示标注工具条了
* 灰度颜色的 Hue 由 -1 改为 0
* 多处细节优化 :)

## v1.10.5
> 2016.12.10

### 新增：
* 颜色卡片增加 HSV 和 HSI 格式

### 修复：
* 从 Chrome 复制的文字粘贴到文字标注框后显示出错 [#257](https://github.com/Snipaste/feedback/issues/257)
* 从 Android Studio 复制的文字转化为贴图后丢失了格式 [#266](https://github.com/Snipaste/feedback/issues/266)
* 特定条件下启动，无法对置顶窗口截图 [#269](https://github.com/Snipaste/feedback/issues/269)
* 贴图之间的层叠次序在重启后有可能没有正确恢复

### 改进：
* "开机启动"已勾选的情况下，启动时若发现开机启动快捷方式丢失，自动尝试重新生成

## v1.10.4
> 2016.12.05

### 修复：
* 特定条件下添加/移除显示器没有被检测到 [#233](https://github.com/Snipaste/feedback/issues/233)
* 对 GIF 文件进行拖拽出错 [#260](https://github.com/Snipaste/feedback/issues/260)
* 特定条件下放大镜消失
* 特定条件下无限弹出更新通知

## v1.10.3
> 2016.12.01

### 新增：
* 全新的 [用户手册](https://docs.snipaste.com/zh-cn)
  * 使用 <a href="https://docsify.js.org/#/zh-cn/" target="_blank">docsify</a> 生成

### 修复：
* 系统设置“拖动时显示窗口内容”未勾选时，无法移动贴图
* 贴出的文件路径无法复制其文字
* 对线条的锚点进行 <kbd>Shift</kbd> 拖动时无效
* [#253](https://github.com/Snipaste/feedback/issues/253)

### 改进：
* 取消对 画笔/马赛克/橡皮擦 的整体移动操作，以防止连续标注时误触

## v1.10.2
> 2016.11.28

### 新增：
* 可随时二次编辑文字标注
* 折线、画笔、马赛克、橡皮擦等形状可以整体拖动
* <kbd>鼠标左键</kbd> 按下 + <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> 可逐像素移动贴图 [#211](https://github.com/Snipaste/feedback/issues/211)
* [命令行选项](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9)：`--paste-files` 可指定文件夹
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：snip/exclude_from_detection
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：snip/crashes

### 修复：
* 程序开机自启后，如手动点检查更新，导致首选项窗口等无响应 [#242](https://github.com/Snipaste/feedback/issues/242)
* 以管理员身份启动时无法响应命令行调用 [#240](https://github.com/Snipaste/feedback/issues/240)
* 特定条件下进入截图后窗口自动选取失效 [#123](https://github.com/Snipaste/feedback/issues/123)


### 改进
* 几处细节修正

## v1.10.1
> 2016.11.22

### 修复：
* 程序一运行即崩溃（小部分用户）
* 回放截图历史时显示有误
* 在线更新后未弹出更新成功的通知

## v1.10
> 2016.11.22

### 新增：
* 支持 [命令行选项](https://github.com/Snipaste/feedback/wiki/%E5%91%BD%E4%BB%A4%E8%A1%8C%E9%80%89%E9%A1%B9)
* 截屏时，允许在编辑模式调整选区 [#211](https://github.com/Snipaste/feedback/issues/211)
* 全部标注形状支持 <kbd>Shift</kbd> 功能
* “折线“新增直线模式：按住 <kbd>鼠标左键</kbd> 并拖动即可
* 托盘图标右键菜单中新增 "帮助" 菜单
* 在贴图上按 <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>C</kbd> 可直接复制纯文本（如果该贴图是由文本转换而来）
* 点击快速保存的气泡通知，可以在文件资源管理器查看文件 [#220](https://github.com/Snipaste/feedback/issues/220)
* 贴图右键菜单，"在文件夹中查看" [#223](https://github.com/Snipaste/feedback/issues/223)
* 选项：以管理员身份开机启动
  * 以便可以在管理员权限的窗口响应快捷键
* 选项：设置代理服务器
* 选项：启动后检测更新的延时
* 选项：每隔一段时间检测更新
* 选项：检查更新时包括 beta 版
* 选项：关闭文字转图片

### 改进：
* **大幅提升截屏效率**
* 重绘所有图标，不再使用图标字体
* 截图时直接贴图的快捷键由 <kbd>Ctrl</kbd> + <kbd>W</kbd> 更改为 <kbd>Ctrl</kbd> + <kbd>T</kbd>
* 轻微调整了放大镜的绘制
* 截屏时工具条的位置调整策略
* 截屏时选区大小标签的位置调整策略

### 修复：
* 增强版托盘菜单中，"显示/隐藏所有贴图" 绑定了错误的动作 [#202](https://github.com/Snipaste/feedback/issues/202)
* 连续切换贴图组可能导致无法贴出新图 [#199](https://github.com/Snipaste/feedback/issues/199)
* 许多小 bug

## v1.9.2
> 2016.10.31

### 修复：
* 移动贴图到另一个组，导致另一个组的贴图丢失 （存在于 1.9 及 1.9.1）
* 将贴图移动到由旧版程序建立的一个组之后，切换过去导致程序崩溃
  * **此次更新后，请循环切换到每个贴图组，以升级贴图的历史记录文件**
* 贴图移组之后，贴图计数可能没有正确更新

### 改进：
* 贴图移动到另一个组之后，将位于原组的所有贴图之上

## v1.9.1
> 2016.10.27

### 修复：

* 贴图操作的用户配置在升级后丢失

### 改进：

* 手动保存文件也应用文件命名规则（但后缀将依然沿用上一次手动保存时候的）

## v1.9
> 2016.10.26

### 新增：

* 自定义图片文件命名规则 [#98](https://github.com/Snipaste/feedback/issues/98)
* 延时截屏 [#181](https://github.com/Snipaste/feedback/issues/181)
* 放大镜可见时，按 <kbd>Shift</kbd> 切换颜色格式 [#187](https://github.com/Snipaste/feedback/issues/187)
* 拖选选区时按住空格键可移动选区 [#192](https://github.com/Snipaste/feedback/issues/192)
* 巴西葡萄牙语翻译（感谢 **Igor Rückert**）
* 波兰语翻译（感谢 **Wirus deleted my username**）

### 修复：

* 连续切换贴图组导致崩溃
* 选项对话框中，改变文字转图片的设置后，其预览图片没有相应更新
* 对标注进行"重做"后，有可能颜色恢复错误
* 多处逻辑错误

### 改进：

* 保证历史记录文件保存时的完整性
* 大大提升连续切换贴图组的效率
* 在自定义贴图操作中禁用 <kbd>Shift</kbd> + <kbd>左键</kbd>（因与贴图对齐冲突）
* 快速保存时在气泡通知中显示文件保存路径
* 记忆选项对话框的位置及当前标签
* 选项对话框尺寸发生变化时使用过渡动画

## v1.8.8
> 2016.10.16

### 修复：

* 快速保存目录路径处理出错（错误地使用了其上一级目录，存在于 1.8.6 及 1.8.7）
* 重启 Snipaste 后没有记住之前的箭头和文字的缩放
* 放大镜出现时，短暂显示了上一次放大镜的内容

## v1.8.7
> 2016.10.15

### 修复：

* 无法播放音效

## v1.8.6
> 2016.10.15

### 新增：

* 选项：截图完成后播放音效
* 编辑时按下 Alt+C 将从屏幕取色并应用到画笔

### 改进：

* 标注被”重做”后自动切换到相应形状的编辑状态，方便二次编辑
* 快速保存路径支持相对路径

## v1.8.5
> 2016.10.13

### 新增：

* 支持包含 <kbd>PrtSc</kbd> 的全局快捷键

### 改进：

* 更改 "Meta" 键的名称为 "Win"

## v1.8.4
> 2016.10.12

### 新增：

* 选项：自定义贴图上的鼠标操作
 * 注意：关闭贴图的默认操作重新调整为"双击"，切换缩略图模式为"Win+左键单击"
 * 如不习惯，请自行设置
* 选项：自定义输出的图片质量
* 更新了内置快捷键列表，并增加了“复制”按钮
* 在贴图上按 <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>P</kbd> 唤出选项窗口
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：General/high_process_priority
* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：snip/do_not_omit_synthesized_c
* 日语翻译 (感谢 **鳳凰院カミ**)

### 修复：

* 连续切换贴图组导致程序崩溃
* 缩略图模式的贴图无法闪烁边框
* 贴图默认的不透明度小于 100 时，贴图判重有误

### 改进：

* 截屏时不再屏蔽所有合成按键（以便 ahk 等软件模拟的按键可正常使用）
* 即使“自动检测界面元素”没有勾选，截屏时也可以通过按 Tab 来检测界面元素
* 长度/宽度很小的贴图也可以很容易被移动了

## v1.8.3
> 2016.10.02

### 修复：

* v1.8.2 使用了错误的截图历史记录文件夹
* 双击托盘图标导致 Snipaste 被强制退出

### 改进：

* 当前激活的贴图窗口的阴影将使用主题色

## v1.8.2
> 2016.09.30

### 新增：

* [高级设置](https://github.com/Snipaste/feedback/wiki/%E9%AB%98%E7%BA%A7%E8%AE%BE%E7%BD%AE)：历史记录文件夹位置

### 修复：

* 在线更新后 Snipaste 没有自动重启（部分用户）
* 从托盘菜单关闭组之后，菜单没有自动隐藏

### 改进：

* 从增强版托盘菜单中移除了“重启”
* 启动时检查并移除历史记录文件夹中未使用到的文件夹

## v1.8.1
> 2016.09.29

### 修复：

* 部分选项在程序重启后没有生效
* （XP）开机启动的快捷键方式在非英文系统没有被成功创建

### 改进：

* 当 config.ini 不可写入时弹出通知提示

## v1.8
> 2016.09.28

### 新增：

* 贴图之缩略图模式（双击它）
* 选项：缩略图大小
* "恢复默认" 按钮
* 可以设置记号笔颜色的透明度了

### 修复：

* 最大化窗口及全屏窗口的范围检测
* 某些高分屏的窗口检测出错
* 双屏（主屏在下，两个屏幕边缘没有对齐）的截屏问题
* 标注形状的控制框被保存到了图像上
* 当颜色为半透明时，矩形/椭圆/折线/画笔 的绘制问题

### 改进：

* 小的颜色按钮不再改变当前颜色的透明度

## v1.7.3
> 2016.09.19

### 修复：
* 识别全屏窗口时没有把任务栏区域包括进去
* （XP）kernel32.dll 错误
* （XP）无法设置全局快捷

### 改进：
* 标注时记住箭头的尺寸
* 启动后恢复上一次的箭头尺寸及文字大小
* 快速保存目录可以手动编辑了
* 窗口检测时忽略不可点击的（透明）窗口
* （XP）消除启动后第一次截图时的窗口动画

## v1.7.2
> 2016.09.17

### 修复：
* 截图时能看到截图窗口的任务栏图标一闪而过
* 一定条件下，刚开始截图时截图区域就被自动确定了
* 截图时光标形状没有及时更新
* 贴图窗口吸附失效（按住 <kbd>Shift</kbd> 键，然后移动贴图窗口并对齐到另一个窗口，然后释放鼠标按键）

## v1.7.1
> 2016.09.15

### 修复：
* "无可用更新"对话框被选项对话框挡住
* 启动时某些 Win 7 x64 用户提示缺少 msvcr120.dll

### 改进：
* 消除截图时直接贴图的延迟

## v1.7
> 2016.09.15

### 新增：

* 自动更新模块
* 截屏时按下 <kbd>Tab</kbd>，可在选取窗口/选取元素之间切换
* 在贴图上按 <kbd>3</kbd> <kbd>4</kbd> 将图像进行镜像翻转
* 选项：截屏时捕捉光标（截图时可按 <kbd>`</kbd> 来切换）
* 选项：截图时单击中键将截图贴到屏幕上 [#63](https://github.com/Snipaste/feedback/issues/63)
* 选项：贴图默认透明度 [#96](https://github.com/Snipaste/feedback/issues/96)
* 选项：以 Windows Bitmap 格式将图像复制到剪贴板 [#20](https://github.com/Snipaste/feedback/issues/20)
  * 如果发现图像无法粘贴到某些应用（如 Skype、UWP 版微信），请打开此选项，但同时会丢失图像的透明通道
* 选项：增强版托盘菜单
* 选项：自定义截屏时的边框宽度
* 选项：在放大镜中显示/隐藏边框
* 选项：在放大镜中显示/隐藏遮罩
* 选项：进入截屏后默认显示全屏十字线
* 选项：截屏时如有其他程序的窗口激活，是否自动退出截屏
* 选项：在贴图右键菜单显示助记键
* 选项：切换到另一分组时自动闪烁贴图

### 修复：

* 截屏时与词典软件的取词、划译等功能的冲突（已测试必应词典、有道词典、QTranslate） [#64](https://github.com/Snipaste/feedback/issues/64)
* 自动检测界面元素时可能导致的截图无响应 [#95](https://github.com/Snipaste/feedback/issues/95) [#116](https://github.com/Snipaste/feedback/issues/116)
* 自动检测窗口对少部分用户无效 [#61](https://github.com/Snipaste/feedback/issues/61)
* 某些情况下开始截图后没有获取到键盘焦点 [#114](https://github.com/Snipaste/feedback/issues/114)
* 特定操作下截图后贴图导致程序崩溃 [#119](https://github.com/Snipaste/feedback/issues/119)
* 试图贴出剪贴板中的超大文件时拷贝副本造成机器卡顿 [#102](https://github.com/Snipaste/feedback/issues/102)
* 在触屏上用手指无法正常截图
* <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>S</kbd> 有时候没反应 [#42](https://github.com/Snipaste/feedback/issues/42)
* 画矩形/椭圆时按下 <kbd>Shift</kbd>，工具条的图标有时没有同步变化 [#53](https://github.com/Snipaste/feedback/issues/53)
* （某种设置下的）高分屏截屏时图片模糊 [#74](https://github.com/Snipaste/feedback/issues/74)
* （某种设置下的）高分屏贴图显示错误 [#74](https://github.com/Snipaste/feedback/issues/74)
* 半透明文字绘制有误
* 橡皮擦及马赛克在背景半透明时绘制有误
* 某些条件下导致贴图重复
* 直接把截图贴到屏幕时，贴图没有被激活（选项有勾选的情况下）
* 保存截图时，边框被画进了截图

### 改进：

* 拖动文字框时默认不旋转，按下 <kbd>Ctrl</kbd> 才可以旋转
* 记忆上一次手动保存图片时的后缀名
* 固定箭头宽度（不再随箭头长度而增加），但用户可通过 <kbd>鼠标滚轮</kbd> 或 <kbd>1</kbd> <kbd>2</kbd> 改变其大小
* 统一截图和贴图的快速保存目录
* 直接点击托盘右键菜单上的分组名切换到分组，同时，关闭、重命名分组请在分组名上右键
* 在大的颜色按钮上增加一个彩色三角，以表示点击后可自定义颜色（By Fubuki）
* “隐藏”单张贴图现在称为“关闭”贴图，以和“隐藏所有贴图”区分开
* 截图界面可见时点击托盘图标将强制退出 Snipaste （以防止万一截图卡住时无法退出 Snipaste）
* 双击空的文字编辑框，也可以直接保存截屏了
* 放大镜将根据当前屏幕的 DPI 自动调整缩放

## v1.6
> 2016.08.10

### 新增：

* 选项：取 Hex 颜色值时不复制 #
* 镂空箭头样式

### 修复：

* 自动探测窗口范围功能，对 Windows 标准样式窗口，识别范围大了一圈
* 自动探测窗口范围功能，对 QQ 窗口，识别范围大了一圈
* 从 Chrome 复制的带特定格式的 HTML 文本，贴出来的图片可能出错
* 屏幕 150% DPI 时，颜色按钮的大小不对
* 放大镜在贴图窗口上移动时有明显延迟

### 改进：

* 更新了部分图标（感谢 像素君）

## v1.5
> 2016.08.08

### 新增：

* 截图、贴图可快速保存到指定目录（按下 <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>S</kbd>，或 <kbd>Shift</kbd> + “保存”按钮）
* 在自动选取窗口区域状态，按下空格键可确定区域
* 截屏时在光标处绘制全屏的十字标线（需按住Alt）
* 截屏时按下 <kbd>Ctrl</kbd> + <kbd>A</kbd> 快捷键，可将截屏区域选择为全屏
* “内置快捷键”窗口（在 选项窗口-热键 标签下）
* 发送截图/贴图到打印机（没有按钮，只有通过 <kbd>Ctrl</kbd> + <kbd>P</kbd>）
* 选项：在放大镜中显示操作提示（默认开启）
* 选项：取色器使用 Hex 颜色值
* 德语翻译（感谢 Samuel Marcius）
* 希腊语翻译（感谢 geogeo.gr）
* 瑞典语翻译（感谢 Åke Engelbrektson）

### 修复：

* 部分快捷键设置显示设置成功，但实际上并没有效果（除 <kbd>PrtScr</kbd> 以及被系统/其他应用程序占用的快捷键，其他快捷键应可顺利被设置）
* 默认全局快捷键被取消之后在一定条件下可能仍会占用
* 双屏、主屏在右时橡皮擦/马赛克/高斯模糊绘制不正确
* 在自动检测界面元素时可能发生程序崩溃
* 颜色对话框没有被翻译
* 按住Alt在多个贴图窗口之间游走时，有些窗口的放大镜没有被自动隐藏
* “使用截图时的位置”这个选项没有影响到截图时的“贴到屏幕”按钮（或 <kbd>Ctrl</kbd> + <kbd>W</kbd>）

### 改进：

* 从贴图复制颜色值时，闪烁一下放大镜以提示颜色复制成功
* 选项“截图时屏蔽 Win 键”，将只屏蔽 <kbd>Win</kbd> 键单击，不会妨碍 <kbd>Win</kbd> + <kbd>空格</kbd> 等组合键的响应
* 从贴图复制颜色值时，保存图像文件时，下拉菜单可以选多种图片格式
* 给当前正在被设置的快捷键编辑框添加（主题色的）边框
* 在快捷键设置对话框，添加了如何删除快捷键的明显提示
* 更改了默认的全局快捷键设置（如贴图为 <kbd>F3</kbd> 而不是 <kbd>F2</kbd>）（只会影响新用户，老用户不会丢失自己的配置）
* 使用 <kbd>↑</kbd> <kbd>←</kbd> <kbd>↓</kbd> <kbd>→</kbd> 移动贴图（而不是 <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd>），以保持和截屏行为的一致
* 使用系统画图程序（mspaint）的调色板

## v1.4
> 2016.08.01

### 新增：

* XP 版
* 使用退格键删除快捷键
* <kbd>Esc</kbd> 隐藏贴图，<kbd>Shift</kbd> + <kbd>Esc</kbd> 销毁贴图
* 选项：按下贴图键时如有任何贴图在闪烁，自动隐藏所有贴图

### 修复：

* <kbd>Shift</kbd> + <kbd>↑</kbd> <kbd>←</kbd> <kbd>↓</kbd> <kbd>→</kbd> 缩小截取范围时出现负数

## v1.3
> 2016.07.29

### 新增：

* <kbd>Shift</kbd> + <kbd>↑</kbd> <kbd>←</kbd> <kbd>↓</kbd> <kbd>→</kbd> 缩小截图区域
* 繁体中文翻译（感谢 zhtw)

### 修复：

* 多屏（主屏在右）截图时按 <kbd>Alt</kbd> 虚线绘制有误
* 多屏情况下，探测的全屏程序窗口超过当前屏幕范围
* 按住 <kbd>Alt</kbd> 时，双击贴图窗口，贴图窗口隐藏后取色窗口还在并无法关闭

## v1.2
> 2016.07.29

### 修复：

* 多屏（主屏在右）的截图问题
* 切换语言后，托盘图标的 tooltip 的语言没有更新

## v1.0
> 2016.07.28

* 首次发布
