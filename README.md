
# 🚀 Portable App Launcher

<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows-blue?style=for-the-badge&logo=windows" alt="Platform" />
  <img src="https://img.shields.io/badge/Built%20With-C%2B%2B%20%2F%20ImGui-orange?style=for-the-badge&logo=c%2B%2B" alt="Built With" />
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="License" />
</p>

---

### 🌟 Overview

**Portable App Launcher** is a gorgeous, hardware-accelerated, and ultra-lightweight desktop dashboard designed to organize and boot your standalone tools, utilities, and games. 

Built with **C++17**, **Dear ImGui**, and **GLFW**, it offers an incredibly fast and fluid user interface while running completely isolated inside its own directory. Perfect for power users who carry their software toolkit on a USB drive!

---

### ✨ Key Features

*   **📦 Zero Installation & Purely Portable**  
    Everything is stored locally. No registry bloat, no hidden appdata folders. Perfect for USB sticks.
*   **🎯 Dynamic Icon Extraction**  
    Automatically pulls high-resolution icons directly from the target `.exe` files and caches them dynamically into OpenGL textures.
*   **⚙️ Smart INI Configuration**  
    A clean, human-readable `.ini` file structure to customize your layout, app paths, arguments, and working directories.
*   **⚡ Hardware Accelerated UI**  
    Powered by OpenGL 3.3 for silky-smooth, 60+ FPS navigation with almost zero CPU overhead.
*   **🔍 Instant Fuzzy Search**  
    Instantly filter through dozens of applications with a highly responsive, real-time search bar.
*   **🗂️ Grid & List Layout Toggles**  
    Switch seamlessly between an elegant grid view showcasing application icons or a compact list view for high-density setups.

---

### 🛠️ Tech Stack & Architecture

This project is engineered with modern C++ and low-level graphics bindings to ensure maximum performance and predictability.

*   **UI Engine:** Dear ImGui (Docking Branch Architecture)
*   **Window & Input:** GLFW 3.3
*   **Graphics API:** OpenGL 3.3 Core Profile
*   **Process Spawning:** Native Win32 `CreateProcessW` / `ShellExecuteExW` (launched as independent detached processes so the UI thread never locks).
*   **Resource Pipeline:** Custom Win32 icon extraction API integrated into the OpenGL texture binding cycle.

---

### ⚙️ The Configuration (`config.ini`)

The launcher automatically generates a default template on its first run. You can easily tweak your dashboard directly from the file:

### 🚀 Quick Start / Compilation

#### 🛸 Prerequisites

Make sure you have **CMake**, a C++17 compliant compiler (MSVC or MinGW), and linked system libraries (`Shell32.lib`, `Ole32.lib` for asset extraction).

```bash
# Clone the repository with dependencies
git clone --recursive [https://github.com/knxwille/Launcher.git](https://github.com/knxwille/Launcher.git)
cd Launcher

# Build project
mkdir build && cd build
cmake ..
cmake --build . --config Release

### 📄 License

Distributed under the **MIT License**. Feel free to fork, modify, and build your own custom dashboards!
