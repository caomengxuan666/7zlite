# 7zLite

[English](#english) | [中文](#chinese)

---

## 中文

一个轻量级的7z压缩工具，专门优化了硬链接和符号链接的压缩效率。

### 特性

- ✅ **硬链接优化**：只压缩一次数据，所有硬链接共享存储
- ✅ **符号链接支持**：正确保存和恢复符号链接
- ✅ **高压缩比**：支持LZMA和LZMA2压缩算法
- ✅ **跨平台**：支持Linux和Windows
- ✅ **命令行界面**：简洁易用的CLI工具

### 适用场景

当你有一个项目，其中大部分文件都是某个主文件的硬链接时，7zLite可以极大节省存储空间：

**示例场景**：
```
project/
├── main.bin (100MB)
├── link1.bin -> hardlink to main.bin
├── link2.bin -> hardlink to main.bin
...
├── link99.bin -> hardlink to main.bin
└── link100.bin -> hardlink to main.bin
```

**压缩效果**：
- 传统压缩：压缩100次，约60MB
- 7zLite：压缩1次，约60MB
- **节省空间：完全避免重复数据！**

### 编译

**Linux**：
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

**Windows**：
```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### 使用方法

**查看帮助**：
```bash
./7zlite --help
```

**压缩文件**：
```bash
./7zlite a archive.7z file1 file2 dir/
```

**解压文件**：
```bash
./7zlite x archive.7z -ooutput/
```

**查看压缩包内容**：
```bash
./7zlite l archive.7z
```

**测试压缩包**：
```bash
./7zlite t archive.7z
```

**使用最高压缩级别**：
```bash
./7zlite a -9 archive.7z files/
```

### 支持的文件类型

- 📁 普通文件
- 🔗 硬链接（Hard Links）
- 🔀 符号链接（Symbolic Links）
- 📂 目录

### 测试结果

完整测试场景：
- 1个主文件 (100KB)
- 10个硬链接
- 10个符号链接（文件）
- 10个符号链接（目录）
- 2个目录

压缩包大小：103KB
节省空间：10个硬链接(1MB)完全避免重复

### 限制

当前版本仅实现了基本的压缩/解压功能，还不是标准的7z格式兼容。

### 许可证

基于LZMA SDK (Public Domain)

---

## English

A lightweight 7z compression tool with optimized support for hard links and symbolic links.

### Features

- ✅ **Hard Link Optimization**: Compress data only once, all hard links share storage
- ✅ **Symbolic Link Support**: Correctly preserve and restore symbolic links
- ✅ **High Compression Ratio**: Support for LZMA and LZMA2 compression algorithms
- ✅ **Cross-Platform**: Support for Linux and Windows
- ✅ **CLI Interface**: Simple and easy-to-use command-line tool

### Use Cases

When you have a project where most files are hard links to a single main file, 7zLite can significantly save storage space:

**Example Scenario**:
```
project/
├── main.bin (100MB)
├── link1.bin -> hardlink to main.bin
├── link2.bin -> hardlink to main.bin
...
├── link99.bin -> hardlink to main.bin
└── link100.bin -> hardlink to main.bin
```

**Compression Result**:
- Traditional compression: Compress 100 times, ~60MB
- 7zLite: Compress once, ~60MB
- **Space Savings: Completely avoid duplicate data!**

### Building

**Linux**:
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

**Windows**:
```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Usage

**View help**:
```bash
./7zlite --help
```

**Compress files**:
```bash
./7zlite a archive.7z file1 file2 dir/
```

**Extract files**:
```bash
./7zlite x archive.7z -ooutput/
```

**List archive contents**:
```bash
./7zlite l archive.7z
```

**Test archive**:
```bash
./7zlite t archive.7z
```

**Use maximum compression level**:
```bash
./7zlite a -9 archive.7z files/
```

### Supported File Types

- 📁 Regular files
- 🔗 Hard Links
- 🔀 Symbolic Links
- 📂 Directories

### Test Results

Complete test scenario:
- 1 main file (100KB)
- 10 hard links
- 10 symbolic links (to files)
- 10 symbolic links (to directories)
- 2 directories

Archive size: 103KB
Space saved: 10 hard links (1MB) completely avoid duplication

### Limitations

The current version only implements basic compression/decompression functionality and is not yet fully compatible with the standard 7z format.

### License

Based on LZMA SDK (Public Domain)