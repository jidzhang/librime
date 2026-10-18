# Windows 源码构建（本项目适配记录）

上游 librime 依赖 boost/leveldb/marisa/opencc/yaml-cpp 等，Windows 下原始流程
较繁琐。以下为本项目在本机（VS2022 + CMake）验证通过的流程，脚本随仓库携带。

## 前置

- Visual Studio 2022（x64 原生工具集 v143）、CMake 3.21+
- 依赖预置在 `deps/`（`install-boost.bat` 拉 boost，其余经 vcpkg/预编）

## 构建

```bat
rem 一次性：配置并编出 rime.dll（Release，含 octagram 语法模型插件）
rebuild-librime2.bat
```

产物：`build/bin/Release/rime.dll`（含 octagram 与 lua 插件）。

## 脚本说明

| 脚本 | 用途 |
|---|---|
| `rebuild-librime2.bat` | 主构建入口：cmake 配置 + 编译 rime 目标（Release） |
| `build-librime-wrapper.bat` | 同上，包装一层（供调用方复用环境） |
| `build-deps-wrapper.bat` | 依赖准备阶段包装 |
| `install-boost.bat` / `install-lua.bat` | 拉取 boost / lua 依赖 |
| `build-tools.bat` / `build-tools2.bat` / `build-tools3.bat` | 构建 tools（`build_grammar` 等） |
| `build-tools/` | tools 构建的中间目录与日志 |
| `env.bat` | 环境变量（RIME_ROOT/BOOST_ROOT/ARCH 等），由 `env.bat.template` 复制 |

## 注意事项（踩坑记录）

1. **源码含中文注释**：`cmake/cxx_flag_overrides.cmake` 强制 `/utf-8`，
   否则 MSVC 按系统代码页解析会把行尾注释尾字节当续行符，产生大面积语法错误。
2. **生成器名含空格**：`-G"%CMAKE_GENERATOR%"` 必须加引号（"Visual Studio 17 2022"）。
3. **共享目录（VMware 共享文件夹）**：相对路径解析偶发失败，构建脚本用绝对路径。
4. **octagram 插件**是独立子仓库（`plugins/librime-octagram`），其改动需单独提交。
