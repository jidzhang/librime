# librime 双架构编译（固定入口，2026-11-15）

> 双击即可，不要另写脚本。四个 bat 全部纯 ASCII + CRLF，日志落在 librime 目录。

## 四个固定入口

| 目标 | 双击 | 产物 | 日志 |
|---|---|---|---|
| x86 deps | `rebuild-deps-x86.bat` | `librime\x86\{lib,include}` | 控制台 |
| x64 deps | `rebuild-deps-x64.bat` | `librime\{lib,include}` | 控制台 |
| x86 rime.dll | `rebuild-librime-x86.bat` | `build-x86\bin\Release\rime.dll` | `..\librime-x86-{configure,build}.log` |
| x64 rime.dll | `rebuild-librime2.bat` | `build\bin\Release\rime.dll` | `..\librime-{reconfigure,build-full2}.log` |

## 硬约束（违反必炸）

1. **两套 deps 前缀必须分离**：x86 → `librime\x86\`，x64 → `librime\{lib,include}`。
   共用前缀会互相覆盖（2026-11-15 实炸：跑 x86 deps 覆盖了 x64 lib，x64 链接 49 个未解析符号）。
2. **deps 只在源码/deps 子模块变动后重编**，平时改 librime 代码只跑 rime 那两个。
3. **glog 已不存在**（2026-11-15 上游换 spdlog facade）。任何脚本里的 glog 步骤都是过时的。
4. **spdlog 是 header-only**，CMake 直接从 `deps/spdlog` 子模块取，无需装前缀、无需单独编。
5. **install target 一律不用**（会往 `C:\Program Files\rime` 装，需要管理员）。bat 里只 build 不 install。
6. **env.bat 的 ARCH 只决定默认值**；每个 bat 自己 `set ARCH=` 覆盖，不要靠改 env.bat 切换架构。
7. 构建目录（`build` / `build-x86`）里的 CMakeCache 绑死平台；换架构必须清对应目录，不要复用。

## 产物去向（编完手动拷，或打包链自动取）

```
build-x86\bin\Release\rime.dll  ->  moqi-ime\input_methods\rime\rime.dll   (产品 daemon, x86)
build\bin\Release\rime.dll      ->  eval-workspace\rime.dll                (白盒评测, x64)
```

打包链（`moqi-im-windows\package-all.bat`）自己从 `librime\build-x86\bin\Release\rime.dll` 取 x86 版，
无需手动拷到 moqi-ime（那份是仓库 tracked 的 x64 产品位）。

## 上游同步后必查

上游合并后（`git merge upstream/master`）先看 `deps/` 有无增删子模块（如 2026-11-15 的 glog→spdlog），
对应更新两个 deps bat 的步骤清单，再重编。
