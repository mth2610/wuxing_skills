#!/usr/bin/env python3
"""Patches an NDK-bundled shaderc source checkout to add the C-API wrapper for
glslang's "relaxed Vulkan rules" mode (shaderc_compile_options_set_vulkan_rules_relaxed).

Why this exists: the Android NDK ships shaderc only as SOURCE (no prebuilt binary at all -
see Makefile.Android's compile_shaderc_android / RLVK_HANDOFF.md §7.18), and the version
bundled with NDK 28 is old enough that libshaderc's own C API never exposed this option -
even though the glslang compiler INSIDE that same source tree already implements it
(glslang::TShader::setEnvInputVulkanRulesRelaxed(), confirmed present in
libshaderc/third_party/glslang/glslang/Public/ShaderLang.h). Rather than fetching a whole
newer shaderc from upstream (network dependency, glslang/SPIRV-Tools/SPIRV-Headers pinned to
a compatible commit, much larger and more fragile), this is a 4-file, ~15-line patch that
just wires the already-compiled-in glslang feature through libshaderc's C API, matching the
exact signature this project's own vendored (newer) shaderc.h already declares
(third_party/vulkan/include/shaderc/shaderc.h) and rlvk_shaderc.inl already calls.

Applied by Makefile.Android's compile_shaderc_android target against a STAGED COPY of the
NDK's shaderc source (never the NDK install itself - the NDK is a shared system install, not
something this project should mutate in place). Idempotent: a marker comment guards every
edit, re-running is a no-op.

Usage: rlvk_patch_shaderc.py <staged_shaderc_source_dir>
"""
import sys

MARKER = "RLVK PATCH"

def patch(path, replacements):
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    if MARKER in src:
        print(f"already patched: {path}")
        return
    for old, new in replacements:
        if old not in src:
            print(f"ERROR: anchor not found in {path}:\n{old}")
            sys.exit(1)
        src = src.replace(old, new, 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print(f"patched: {path}")

root = sys.argv[1]

# --- libshaderc/include/shaderc/shaderc.h: declare the C API function ---
patch(f"{root}/libshaderc/include/shaderc/shaderc.h", [
    (
        'SHADERC_EXPORT void shaderc_compile_options_set_auto_map_locations(\n'
        '    shaderc_compile_options_t options, bool auto_map);\n',

        'SHADERC_EXPORT void shaderc_compile_options_set_auto_map_locations(\n'
        '    shaderc_compile_options_t options, bool auto_map);\n'
        '\n'
        '// ' + MARKER + ': relaxes Vulkan GLSL validation to accept stock GL-dialect\n'
        '// constructs (e.g. non-opaque uniforms outside a block) - see rlvk_patch_shaderc.py.\n'
        'SHADERC_EXPORT void shaderc_compile_options_set_vulkan_rules_relaxed(\n'
        '    shaderc_compile_options_t options, bool enable);\n',
    ),
])

# --- libshaderc_util/include/libshaderc_util/compiler.h: option storage + setter ---
patch(f"{root}/libshaderc_util/include/libshaderc_util/compiler.h", [
    (
        '        auto_map_locations_(false),\n',
        '        auto_map_locations_(false),\n'
        '        vulkan_rules_relaxed_(false),  // ' + MARKER + '\n',
    ),
    (
        '  void SetAutoMapLocations(bool auto_map) { auto_map_locations_ = auto_map; }\n',

        '  void SetAutoMapLocations(bool auto_map) { auto_map_locations_ = auto_map; }\n'
        '\n'
        '  // ' + MARKER + '\n'
        '  void SetVulkanRulesRelaxed(bool enable) { vulkan_rules_relaxed_ = enable; }\n',
    ),
    (
        '  bool auto_map_locations_;\n',
        '  bool auto_map_locations_;\n'
        '\n'
        '  // ' + MARKER + '\n'
        '  bool vulkan_rules_relaxed_;\n',
    ),
])

# --- libshaderc_util/src/compiler.cc: thread the option into glslang::TShader ---
patch(f"{root}/libshaderc_util/src/compiler.cc", [
    (
        '  shader.setAutoMapLocations(auto_map_locations_);\n',

        '  shader.setAutoMapLocations(auto_map_locations_);\n'
        '  if (vulkan_rules_relaxed_) shader.setEnvInputVulkanRulesRelaxed();  // ' + MARKER + '\n',
    ),
])

# --- libshaderc/src/shaderc.cc: the C API wrapper itself ---
patch(f"{root}/libshaderc/src/shaderc.cc", [
    (
        'void shaderc_compile_options_set_auto_map_locations(\n'
        '    shaderc_compile_options_t options, bool auto_map) {\n'
        '  options->compiler.SetAutoMapLocations(auto_map);\n'
        '}\n',

        'void shaderc_compile_options_set_auto_map_locations(\n'
        '    shaderc_compile_options_t options, bool auto_map) {\n'
        '  options->compiler.SetAutoMapLocations(auto_map);\n'
        '}\n'
        '\n'
        '// ' + MARKER + '\n'
        'void shaderc_compile_options_set_vulkan_rules_relaxed(\n'
        '    shaderc_compile_options_t options, bool enable) {\n'
        '  options->compiler.SetVulkanRulesRelaxed(enable);\n'
        '}\n',
    ),
])

# --- glslang/MachineIndependent/ShaderLang.cpp: the flag above is a dead end without this ---
# TShader::setEnvInputVulkanRulesRelaxed() only ever sets environment.input.vulkanRulesRelaxed;
# TranslateEnvironment() only copies that into the live spvVersion.vulkanRelaxed (the value the
# actual "non-opaque uniforms outside a block" check reads, ParseHelper.cpp
# transparentOpaqueCheck) inside the `case EShClientVulkan:` branch of a switch on
# environment.input.dialect - which is only reached if TShader::setEnvInput() was called first
# to set environment.input.languageFamily != EShSourceNone. libshaderc's compiler.cc NEVER
# calls setEnvInput() at all (confirmed: zero references) - it only calls setEnvClient/
# setEnvTarget, which don't touch environment.input. So the option-storage/wrapper patches
# above alone are necessary but not sufficient - the flag reaches TShader but the environment
# translation silently drops it before the parser ever sees it. Fix: apply
# environment->input.vulkanRulesRelaxed to spvVersion.vulkanRelaxed unconditionally, rather
# than gated behind the dialect switch libshaderc never populates.
patch(f"{root}/third_party/glslang/glslang/MachineIndependent/ShaderLang.cpp", [
    (
        '    if (messages & EShMsgVulkanRules) {\n'
        '        spvVersion.vulkan = EShTargetVulkan_1_0;\n'
        '        spvVersion.vulkanGlsl = 100;\n'
        '    } else if (spvVersion.spv != 0)\n'
        '        spvVersion.openGl = 100;\n',

        '    if (messages & EShMsgVulkanRules) {\n'
        '        spvVersion.vulkan = EShTargetVulkan_1_0;\n'
        '        spvVersion.vulkanGlsl = 100;\n'
        '    } else if (spvVersion.spv != 0)\n'
        '        spvVersion.openGl = 100;\n'
        '    // ' + MARKER + ': libshaderc never calls TShader::setEnvInput(), so the\n'
        '    // dialect-switch branch below that would normally copy this flag never runs -\n'
        '    // apply it unconditionally instead. See rlvk_patch_shaderc.py for the full story.\n'
        '    if (environment != nullptr)\n'
        '        spvVersion.vulkanRelaxed = environment->input.vulkanRulesRelaxed;\n',
    ),
])

print("shaderc rlvk patch complete")
