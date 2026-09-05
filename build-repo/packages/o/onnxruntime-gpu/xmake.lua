-- Local fork of xrepo's onnxruntime package, GPU (CUDA-12) flavor, minus the
-- `cuda` build dependency: the encro binary only links the import lib, and the
-- CUDA/cuDNN DLLs are a runtime concern (scoop cuda12.9 + self-installed
-- cuDNN, see the organize design). The xrepo `cuda` dep is also unsupported
-- under MSYS-shell package envs, which broke builds from Git Bash.
package("onnxruntime-gpu")
    set_homepage("https://www.onnxruntime.ai")
    set_description("ONNX Runtime GPU (CUDA execution provider) prebuilt binaries")
    set_license("MIT")

    add_configs("shared", {description = "Download shared binaries.", default = true, type = "boolean", readonly = true})

    if is_plat("windows") and is_arch("x64") then
        set_urls("https://github.com/microsoft/onnxruntime/releases/download/v$(version)/onnxruntime-win-x64-gpu-$(version).zip")
        add_versions("1.22.1", "4e6eeb8bfe4137cf98ccdc0b01f0400928bc3f8261b90e8e0d1c28410a33cac4")
    end

    on_install("windows|x64", function (package)
        local cmake_file = "lib/cmake/onnxruntime/onnxruntimeTargets.cmake"
        if os.isfile(cmake_file) then
            io.replace(cmake_file, "include/onnxruntime", "include", {plain = true})
        end
        cmake_file = "lib/cmake/onnxruntime/onnxruntimeTargets-release.cmake"
        if os.isfile(cmake_file) then
            io.replace(cmake_file, "lib64", "lib", {plain = true})
        end
        if package:is_plat("windows") then
            os.mv("lib/*.dll", package:installdir("bin"))
        end
        os.cp("*", package:installdir())
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <array>
            #include <cstdint>
            void test() {
                std::array<float, 2> data = {0.0f, 0.0f};
                std::array<int64_t, 1> shape{2};
                Ort::Env env;
                auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
                auto tensor = Ort::Value::CreateTensor<float>(memory_info, data.data(), data.size(), shape.data(), shape.size());
            }
        ]]}, {configs = {languages = "c++17"}, includes = "onnxruntime_cxx_api.h"}))
    end)
