# NetSpeedCtrl - 网络速度控制工具

## 项目简介

NetSpeedCtrl 是一个基于 WinDivert 的网络速度限制工具，可以帮助用户控制网络连接的速度，实现带宽管理功能。

## 功能特性

- 基于 WinDivert 内核驱动的网络流量拦截
- 支持 TCP/UDP 协议的速度限制
- 实时网络流量监控
- 简单易用的命令行界面

## 系统要求

- Windows 操作系统
- WinDivert 驱动支持
- CMake 构建工具
- GCC 或 MSVC 编译器

## 构建步骤

### 使用 GCC 构建

运行 `build_gcc.bat` 脚本：

```bash
build_gcc.bat
```

### 使用 MSVC 构建

运行 `build_msvc.bat` 脚本：

```bash
build_msvc.bat
```

构建完成后，会生成 `NetSpeedCtrl.exe` 可执行文件。

## 使用方法

1. 以管理员权限运行 `NetSpeedCtrl.exe`
2. 程序会显示当前网络接口信息
3. 根据提示选择要限制的网络接口
4. 设置下载和上传速度限制（单位：Mbps）
5. 程序开始工作，按 Ctrl+C 停止

### 示例

```bash
NetSpeedCtrl.exe
```

## 项目结构

- `src/` - 源代码文件
  - `main.c` - 主程序入口
  - `network.c` - 网络处理逻辑
  - `resource.rc` - 资源文件
- `include/` - 头文件
- `WinDivert/` - WinDivert 库文件
- `CMakeLists.txt` - CMake 构建配置

## 依赖项

- WinDivert: https://github.com/basil00/Divert

## 许可证

本项目采用 MIT 许可证。

## 贡献

欢迎提交 Issue 和 Pull Request！

## 版本历史

- v1.0 - 初始版本，支持基本速度限制功能