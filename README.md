# LibSpecialDrive

**LibSpecialDrive** is a lightweight C library designed to identify and manage special-purpose drives using the **protective MBR**.  
It automatically detects USB drives, recognizes their partitions, and retrieves mount points for seamless integration with Python applications.

## Features

- Cross-platform support: **macOS**, **Linux**, and **Windows**
- Fully written in **C** with no external dependencies
- Uses **CMake** for easy build and configuration
- Automatically detects and tags drives using the **protective MBR**
- Provides convenient APIs for integration with higher-level applications (e.g., Python)

## Build Instructions

```bash
mkdir build
cd build
cmake ..
make
sudo make install
```
