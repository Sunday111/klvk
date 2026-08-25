# Getting Started

klvk is a YAE package rather than a standalone YAE project. Add it to a project that contains `yae_project.json`, then
link its `klvk` module from an executable or library. The package also contributes all executables under `examples/`.

## Requirements

- A C++23 compiler, CMake, Ninja, Python 3, and [YAE](https://github.com/Sunday111/yae).
- A Vulkan 1.3-capable driver.
- A Vulkan loader and the platform development libraries required to build GLFW.
- FFmpeg development packages exposing `libavcodec`, `libavformat`, `libavutil`, and `libswscale` through `pkg-config`.
- Linux x86-64 for the binary Slang package currently pinned by klvk. Other hosts require a corresponding artifact in
  the Slang package.

YAE fetches Vulkan headers, GLFW, FreeType, and the other source and binary package dependencies. System libraries
remain the responsibility of the host.

## Add the package

In the consumer's `*.package.json`, declare klvk:

```json
{
    "modules_dir": "src",
    "dependencies": {
        "packages": [
            {
                "link": "https://github.com/Sunday111/klvk main",
                "packages": ["klvk"]
            }
        ]
    }
}
```

Then add `klvk` to the relevant module's dependencies:

```json
{
    "ModuleType": "Executable",
    "Dependencies": {
        "Private": ["klvk"]
    },
    "CopyDirectoriesAfterBuild": ["content"]
}
```

Put runtime assets under the module's `content/` directory. YAE stages linked content into `<build>/bin/content`.
klvk resolves its default shader and cache paths relative to the executable, so applications do not depend on the
directory from which they are launched.

## A minimal application

Derive from `klvk::Application`, initialize resources after the base application, and record drawing commands in
`Tick`:

```cpp
#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/window.hpp"

class ExampleApp final : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();
        GetWindow().SetTitle("Example");
        SetClearColor({0.08f, 0.1f, 0.14f, 1.f});
    }

    void Tick() override
    {
        klvk::Application::Tick();
        // Record Vulkan commands through GetCurrentCommandBuffer().
    }
};

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(
        [](int app_argc, char** app_argv)
        {
            ExampleApp app;
            app.RunWithArguments(app_argc, app_argv);
        },
        argc,
        argv);
}
```

Calling the base lifecycle methods preserves klvk's own frame and ImGui work. Use `RunWithArguments` rather than
`Run` when the executable should accept [diagnostic command-line options](diagnostics.md).

## Build and run

Run YAE from the consumer project, not from the klvk checkout:

```sh
yae build my_application
yae run my_application
```

Because klvk has no `yae_project.json`, it cannot configure or build itself. Its examples are discovered through the
package, so a consumer can list and run them by target name:

```sh
yae list --all
yae run klvk_minimal_quad_example
```

The target name is the `*.module.json` filename without the suffix. For example, `examples/curve` produces
`klvk_curve_example`, not `curve`.

Continue with the [application model](application.md), then the [rendering guide](rendering.md). The
[`minimal_quad`](../examples/minimal_quad/code/private/minimal_quad_example.cpp) example is the shortest complete
graphics pipeline.
