# 7zLite

[English](#english) | [中文](#chinese)

---

## 中文

一个轻量级的7z压缩工具，专门优化了硬链接和符号链接的压缩效率。

### 特性

- ✅ **标准 7z 格式支持**：可以解压标准 7z 文件（7-Zip、WinRAR、PeaZip 等工具创建的）
- ✅ **自动格式检测**：自动识别归档格式（标准 7z 或自定义格式）
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

**解压文件**（自动检测格式）：
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

### 归档格式说明

7zLite 支持两种归档格式：

#### 1. 标准 7z 格式（解压时自动检测）
- **兼容性**：可由 7-Zip、WinRAR、PeaZip 等工具解压
- **压缩**：使用标准 LZMA/LZMA2 算法
- **硬链接**：解压后不保留硬链接（每个文件独立）
- **适用场景**：需要与其他工具兼容的归档

#### 2. 自定义格式（压缩时使用）
- **兼容性**：仅可由 7zLite 解压
- **压缩**：使用 LZMA/LZMA2 算法 + 硬链接优化
- **硬链接**：解压后保留硬链接关系
- **适用场景**：需要最大化节省存储空间的归档

**对比示例**：
```
标准 7z 格式：WinuxCmd-0.4.1-win-x64.7z (350KB)
  ↓ 解压后 36MB
  ↓ 所有工具都能解压

自定义格式：WinuxCmd-hardlink.7z (350KB)
  ↓ 解压后 1MB
  ↓ 只有 7zLite 能解压
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

- **压缩**：使用自定义格式（支持硬链接优化），不完全兼容标准 7z 格式
- **解压**：完全兼容标准 7z 格式和自定义格式
- **建议**：如需与他人分享归档，建议使用标准 7z 工具（如 7-Zip）创建标准格式归档，然后让用户自己创建硬链接

### 许可证

基于LZMA SDK (Public Domain)

---

## English

A lightweight 7z compression tool with optimized support for hard links and symbolic links.

### Features

- ✅ **Standard 7z Format Support**: Extract standard 7z archives (created by 7-Zip, WinRAR, PeaZip, etc.)
- ✅ **Auto-detect Archive Format**: Automatically detects standard 7z or custom format
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

**Extract files** (auto-detect format):
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

### Archive Formats

7zLite supports two archive formats:

#### 1. Standard 7z Format (auto-detected when extracting)
- **Compatibility**: Can be extracted by 7-Zip, WinRAR, PeaZip, etc.
- **Compression**: Uses standard LZMA/LZMA2 algorithms
- **Hard Links**: Not preserved after extraction (each file is independent)
- **Use Case**: Archives that need compatibility with other tools

#### 2. Custom Format (used when compressing)
- **Compatibility**: Can only be extracted by 7zLite
- **Compression**: Uses LZMA/LZMA2 algorithms + hard link optimization
- **Hard Links**: Preserved after extraction
- **Use Case**: Archives that need maximum space savings

**Example Comparison**:
```
Standard 7z Format: WinuxCmd-0.4.1-win-x64.7z (350KB)
  ↓ Extracts to 36MB
  ↓ All tools can extract

Custom Format: WinuxCmd-hardlink.7z (350KB)
  ↓ Extracts to 1MB
  ↓ Only 7zLite can extract
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

- **Compression**: Uses custom format (with hard link optimization), not fully compatible with standard 7z format
- **Extraction**: Fully compatible with both standard 7z format and custom format
- **Recommendation**: For sharing archives with others, consider using standard 7z tools (like 7-Zip) to create standard format archives, and let users create hard links themselves if needed

### License

Based on LZMA SDK (Public Domain)