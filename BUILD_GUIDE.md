# RDPCallRecorder v2.6.5 Build Guide

This guide provides step-by-step instructions for compiling the `RDPCallRecorder` application and building the NSIS installer on a Windows machine.

## 1. Prerequisites

Ensure the following software is installed on your system:

- **Visual Studio 2019 or later**: Required for the C++ compiler (MSVC). Make sure to install the **"Desktop development with C++"** workload.
- **CMake**: A cross-platform build system generator. Download from [cmake.org](https://cmake.org/download/). Make sure `cmake.exe` is in your system's PATH.
- **Git for Windows**: Required for cloning the source code repositories. Download from [git-scm.com](https://git-scm.com/download/win).
- **NSIS (Nullsoft Scriptable Install System)**: Required only for building the final `Setup.exe` installer. Download from [nsis.sourceforge.io](https://nsis.sourceforge.io/Download).

## 2. Cloning the Repositories

First, create a root directory for the projects (e.g., `C:\Projects`). Then, clone both the main application repository and its required dependency, `AudioCapture`.

```batch
:: Open Command Prompt or PowerShell

:: 1. Create a directory to hold the projects
cd C:\
md Projects
cd Projects

:: 2. Clone the main application repository
git clone https://github.com/vaildavis917-cell/RDPCallRecorder.git

:: 3. Clone the required AudioCapture dependency
git clone https://github.com/masonasons/AudioCapture.git
```

After this step, your directory structure should look like this:

```
C:\Projects\
|-- RDPCallRecorder\
|   |-- src\
|   |-- installer\
|   |-- CMakeLists.txt
|   `-- ...
`-- AudioCapture\
    |-- include\
    |-- src\
    `-- ...
```

## 3. Building the Application (RDPCallRecorder.exe)

We will use CMake to generate a Visual Studio solution and then build the executable.

```batch
:: 1. Navigate into the application's directory
cd C:\Projects\RDPCallRecorder

:: 2. Create a build directory
md build
cd build

:: 3. Run CMake to generate the Visual Studio solution.
::    This command points to the parent directory for CMakeLists.txt
::    and tells CMake where to find the AudioCapture dependency.
cmake .. -G "Visual Studio 16 2019" -A x64 -DAUDIOCAPTURE_DIR=C:\Projects\AudioCapture

:: 4. Build the project using CMake's build tool (or open the .sln in Visual Studio)
::    This command builds the Release configuration.
cmake --build . --config Release
```

- **Note on Visual Studio Version**: If you are using a different version of Visual Studio, change the generator name in the `cmake` command (e.g., `"Visual Studio 17 2022"`).
- **Output**: The compiled `RDPCallRecorder.exe` will be located in `C:\Projects\RDPCallRecorder\build\bin\Release\`.

## 4. Building the Installer (RDPCallRecorder_Setup.exe)

After successfully building the application, you can create the final installer.

1.  **Copy the Executable**: Copy the `RDPCallRecorder.exe` from the build output directory (`C:\Projects\RDPCallRecorder\build\bin\Release\`) into the installer's source directory (`C:\Projects\RDPCallRecorder\installer\files\`).

    ```batch
    xcopy C:\Projects\RDPCallRecorder\build\bin\Release\RDPCallRecorder.exe C:\Projects\RDPCallRecorder\installer\files\ /Y
    ```

2.  **Run NSIS**: Right-click on the installer script `C:\Projects\RDPCallRecorder\installer\installer.nsi` and select **"Compile NSIS Script"**. 

    This will generate `RDPCallRecorder_Setup.exe` in the `C:\Projects\RDPCallRecorder\installer\` directory.

## 5. Uploading to GitHub Release

Once you have the final `RDPCallRecorder_Setup.exe`, you can upload it to the draft release on GitHub:

1.  Go to the **[v2.6.5 draft release page](https://github.com/vaildavis917-cell/RDPCallRecorder/releases/edit/untagged-06349464b5efef38f287)**.
2.  In the "Attach binaries by dropping them here or selecting them" section, upload the `RDPCallRecorder_Setup.exe` file.
3.  Review the release notes and click **"Publish release"**.
