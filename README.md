# CCP Module - PDM

Platform Detection Module (PDM) is an OS agnostic library for gathering metrics about the current machine.

## Building

The project can be built using the normal CMake flow:

```shell
> cmake -DCMAKE_BUILD_TYPE=Release --preset arm64-linux-release -S /path/to/pdm/sources -B /path/to/build/folder
> cmake --build /path/to/build/folder --target all
```

There are a variety of presets available, you can list ones available for your current platform using:
```shell
> cmake --list-presets
```

Alternately, look at `CMakePresets.json` to see an overview of presets for all platform

## 🤝 Contributing
Contribution follows the standard GIT PR model.

By submitting a pull request or otherwise contributing to this project, you agree to license your contribution under the MIT License, and you confirm that you have the right to do so.

## 📄 License and Legal Notices

© 2026 CCP Games 

This software is provided by CCP Games and does not include or distribute any third-party libraries or frameworks. 

This software is a Platform Detection Module, an OS agnostic library for data collection

Trademark Notice: CCP Games is a trademark of CCP ehf. 

This project is licensed under the [MIT License](LICENSE.md). Nothing in the [MIT License](LICENSE.md) grants any rights to CCP Games' trademarks or game content.
