/**
 * @file        codegen/codegen_writer.cpp
 * @brief       Consolidated codegen output writer
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/codegen/codegen_writer.h>
#include "codegen_flags.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <inja/inja.hpp>

#include <rex/codegen/function_graph.h>
#include <rex/codegen/template_registry.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/export_resolver.h>

#include "codegen_logging.h"
#include "template_registry_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include <xxhash.h>

namespace {

void writeU16(FILE* file, uint16_t value) {
  const uint8_t bytes[2] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
  };
  fwrite(bytes, sizeof(bytes), 1, file);
}

void writeU32(FILE* file, uint32_t value) {
  const uint8_t bytes[4] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF),
  };
  fwrite(bytes, sizeof(bytes), 1, file);
}

bool writeBmpIco(const std::filesystem::path& iconPath, const std::vector<uint8_t>& pngData) {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* rgbaPixels =
      stbi_load_from_memory(pngData.data(), static_cast<int>(pngData.size()), &width, &height,
                            &channels, 4);
  if (!rgbaPixels) {
    REXCODEGEN_WARN("Failed to decode PNG icon data: {}", stbi_failure_reason());
    return false;
  }

  if (width <= 0 || height <= 0) {
    stbi_image_free(rgbaPixels);
    REXCODEGEN_WARN("Decoded icon has invalid dimensions: {}x{}", width, height);
    return false;
  }

  const uint32_t xorStride = static_cast<uint32_t>(width) * 4;
  const uint32_t xorSize = xorStride * static_cast<uint32_t>(height);
  const uint32_t andStride = ((static_cast<uint32_t>(width) + 31u) / 32u) * 4u;
  const uint32_t andSize = andStride * static_cast<uint32_t>(height);
  const uint32_t dibSize = 40u + xorSize + andSize;
  const uint8_t iconWidth = width >= 256 ? 0 : static_cast<uint8_t>(width);
  const uint8_t iconHeight = height >= 256 ? 0 : static_cast<uint8_t>(height);

  FILE* iconFile = fopen(iconPath.string().c_str(), "wb");
  if (!iconFile) {
    stbi_image_free(rgbaPixels);
    return false;
  }

  writeU16(iconFile, 0);
  writeU16(iconFile, 1);
  writeU16(iconFile, 1);

  fwrite(&iconWidth, 1, 1, iconFile);
  fwrite(&iconHeight, 1, 1, iconFile);
  const uint8_t colorCount = 0;
  const uint8_t reserved = 0;
  fwrite(&colorCount, 1, 1, iconFile);
  fwrite(&reserved, 1, 1, iconFile);
  writeU16(iconFile, 1);
  writeU16(iconFile, 32);
  writeU32(iconFile, dibSize);
  writeU32(iconFile, 6 + 16);

  writeU32(iconFile, 40);
  writeU32(iconFile, static_cast<uint32_t>(width));
  writeU32(iconFile, static_cast<uint32_t>(height * 2));
  writeU16(iconFile, 1);
  writeU16(iconFile, 32);
  writeU32(iconFile, 0);
  writeU32(iconFile, xorSize);
  writeU32(iconFile, 0);
  writeU32(iconFile, 0);
  writeU32(iconFile, 0);
  writeU32(iconFile, 0);

  for (int y = height - 1; y >= 0; --y) {
    const stbi_uc* row = rgbaPixels + (static_cast<size_t>(y) * static_cast<size_t>(width) * 4);
    for (int x = 0; x < width; ++x) {
      const stbi_uc* pixel = row + (static_cast<size_t>(x) * 4);
      const uint8_t bgra[4] = {pixel[2], pixel[1], pixel[0], pixel[3]};
      fwrite(bgra, sizeof(bgra), 1, iconFile);
    }
  }

  std::vector<uint8_t> andMask(andSize, 0);
  for (int y = 0; y < height; ++y) {
    const stbi_uc* row = rgbaPixels + (static_cast<size_t>(height - 1 - y) * static_cast<size_t>(width) * 4);
    uint8_t* maskRow = andMask.data() + (static_cast<size_t>(y) * andStride);
    for (int x = 0; x < width; ++x) {
      const stbi_uc alpha = row[static_cast<size_t>(x) * 4 + 3];
      if (alpha == 0) {
        maskRow[x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
      }
    }
  }
  fwrite(andMask.data(), andMask.size(), 1, iconFile);

  fclose(iconFile);
  stbi_image_free(rgbaPixels);
  return true;
}

nlohmann::json buildTemplateData(const rex::codegen::CodegenContext& ctx,
                                 const std::vector<const rex::codegen::FunctionNode*>& functions,
                                 const std::unordered_map<uint32_t, std::string>& rexcrtByAddr) {
  const auto& cfg = ctx.Config();

  // Compute code_base and code_size from binary sections
  size_t codeMin = ~size_t(0);
  size_t codeMax = 0;
  for (const auto& section : ctx.binary().sections()) {
    if (section.executable) {
      if (section.baseAddress < codeMin)
        codeMin = section.baseAddress;
      if ((section.baseAddress + section.size) > codeMax)
        codeMax = section.baseAddress + section.size;
    }
  }

  // Build functions JSON array
  nlohmann::json functionsJson = nlohmann::json::array();
  for (const auto* fn : functions) {
    std::string funcName;
    bool isRexcrt = false;

    auto crtIt = rexcrtByAddr.find(static_cast<uint32_t>(fn->base()));
    if (crtIt != rexcrtByAddr.end()) {
      funcName = crtIt->second;
      isRexcrt = true;
    } else if (fn->base() == ctx.analysisState().entryPoint) {
      funcName = "xstart";
    } else if (!fn->name().empty()) {
      funcName = fn->name();
    } else {
      funcName = fmt::format("sub_{:08X}", fn->base());
    }

    functionsJson.push_back({
        {"address", fmt::format("0x{:X}", fn->base())},
        {"name", funcName},
        {"is_rexcrt", isRexcrt},
        {"below_code_base", (fn->base() < codeMin)},
        {"is_import", false},
    });
  }

  // Build imports JSON array
  nlohmann::json importsJson = nlohmann::json::array();
  for (const auto& [addr, node] : ctx.graph.functions()) {
    if (node->authority() != rex::codegen::FunctionAuthority::IMPORT)
      continue;
    importsJson.push_back({
        {"address", fmt::format("0x{:X}", addr)},
        {"name", node->name()},
    });
  }

  // Build config flags
  nlohmann::json configFlags = {
      {"skip_lr", cfg.skipLr},
      {"ctr_as_local", cfg.ctrAsLocalVariable},
      {"xer_as_local", cfg.xerAsLocalVariable},
      {"reserved_as_local", cfg.reservedRegisterAsLocalVariable},
      {"skip_msr", cfg.skipMsr},
      {"cr_as_local", cfg.crRegistersAsLocalVariables},
      {"non_argument_as_local", cfg.nonArgumentRegistersAsLocalVariables},
      {"non_volatile_as_local", cfg.nonVolatileRegistersAsLocalVariables},
  };

  bool hasIcon = !ctx.gameIcon().empty();
  std::string iconFilename = hasIcon ? fmt::format("{}_icon.ico", cfg.projectName) : "";

  return {
      {"project", cfg.projectName},
      {"image_base", fmt::format("0x{:X}", ctx.binary().baseAddress())},
      {"image_size", fmt::format("0x{:X}", ctx.binary().imageSize())},
      {"code_base", fmt::format("0x{:X}", codeMin)},
      {"code_size", fmt::format("0x{:X}", codeMax - codeMin)},
      {"rexcrt_heap", cfg.rexcrtFunctions.contains("RtlAllocateHeap") ? 1 : 0},
      {"config_flags", configFlags},
      {"functions", functionsJson},
      {"imports", importsJson},
      {"recomp_files", nlohmann::json::array()},
      {"has_icon", hasIcon},
      {"icon_filename", iconFilename},
      {"game_title", ctx.gameTitle()},
  };
}

}  // namespace

namespace rex::codegen {

constexpr size_t kOutputBufferReserveSize = 32 * 1024 * 1024;  // 32 MB

CodegenWriter::CodegenWriter(CodegenContext& ctx, Runtime* runtime)
    : ctx_(ctx), runtime_(runtime) {}

// Convenience accessors
FunctionGraph& CodegenWriter::graph() {
  return ctx_.graph;
}
const FunctionGraph& CodegenWriter::graph() const {
  return ctx_.graph;
}
const BinaryView& CodegenWriter::binary() const {
  return ctx_.binary();
}
RecompilerConfig& CodegenWriter::config() {
  return ctx_.Config();
}
const RecompilerConfig& CodegenWriter::config() const {
  return ctx_.Config();
}
AnalysisState& CodegenWriter::analysisState() {
  return ctx_.analysisState();
}
const AnalysisState& CodegenWriter::analysisState() const {
  return ctx_.analysisState();
}

bool CodegenWriter::write(bool force) {
  // --- Validation gate (from recompile.cpp) ---
  if (ctx_.errors.HasErrors() && !force) {
    REXCODEGEN_ERROR("Code generation blocked: {} validation errors. Use --force to override.",
                     ctx_.errors.Count());
    return false;
  }

  // --- Output directory setup (from recompile.cpp) ---
  std::filesystem::path outputPath = ctx_.configDir() / config().outDirectoryPath;
  REXCODEGEN_INFO("Output path: {}", outputPath.string());
  std::filesystem::create_directories(outputPath);

  // --- Clean old generated files (from recompile.cpp) ---
  std::string prefix = config().projectName + "_";
  for (const auto& entry : std::filesystem::directory_iterator(outputPath)) {
    auto ext = entry.path().extension();
    if (ext == ".cpp" || ext == ".h" || ext == ".cmake" || ext == ".rc") {
      std::string filename = entry.path().filename().string();
      if (filename == "sources.cmake" || filename.starts_with(prefix) ||
          filename.starts_with("ppc_recomp") || filename.starts_with("ppc_func_mapping") ||
          filename.starts_with("function_table_init") || filename.starts_with("ppc_config")) {
        std::filesystem::remove(entry.path());
      }
    }
  }

  // --- Write game icon extracted from XDBF ---
  // XDBF stores the title icon as PNG. Convert it to a classic BMP-based ICO
  // because the Windows resource compiler does not accept raw PNG files or
  // PNG-in-ICO payloads here.
  if (!ctx_.gameIcon().empty()) {
    auto iconPath = outputPath / fmt::format("{}_icon.ico", config().projectName);
    if (writeBmpIco(iconPath, ctx_.gameIcon())) {
      REXCODEGEN_INFO("Wrote game icon: {}", iconPath.string());
    } else {
      REXCODEGEN_WARN("Failed to write game icon: {}", iconPath.string());
    }
  }

  // --- Everything below from recompiler.cpp recompile() ---
  REXCODEGEN_TRACE("Recompile: starting");
  out.reserve(kOutputBufferReserveSize);

  // Build sorted function list from graph
  std::vector<const FunctionNode*> functions;
  functions.reserve(graph().functionCount());
  for (const auto& [addr, node] : graph().functions()) {
    functions.push_back(node.get());
  }
  std::sort(functions.begin(), functions.end(),
            [](const auto* a, const auto* b) { return a->base() < b->base(); });

  // Build rexcrt reverse map and rename graph nodes
  std::unordered_map<uint32_t, std::string> rexcrtByAddr;
  for (const auto& [name, addr] : config().rexcrtFunctions) {
    auto crtName = fmt::format("rexcrt_{}", name);
    rexcrtByAddr[addr] = crtName;
    if (auto* node = graph().getFunction(addr)) {
      node->setName(std::move(crtName));
    }
  }

  const std::string& projectName = config().projectName;

  TemplateRegistry registry;
  if (!config().templateDir.empty())
    registry.loadOverrides(config().templateDir);

  auto tmplData = buildTemplateData(ctx_, functions, rexcrtByAddr);

  // Generate {project}_config.h
  REXCODEGEN_TRACE("Recompile: generating {}_config.h", projectName);
  out = renderWithJson(registry, "codegen/config_h", tmplData);
  SaveCurrentOutData(fmt::format("{}_config.h", projectName));

  // Generate {project}_init.h
  REXCODEGEN_TRACE("Recompile: generating {}_init.h", projectName);
  out = renderWithJson(registry, "codegen/init_h", tmplData);
  SaveCurrentOutData(fmt::format("{}_init.h", projectName));

  // Generate {project}_init.cpp
  REXCODEGEN_TRACE("Recompile: generating {}_init.cpp (function mapping table)", projectName);
  out = renderWithJson(registry, "codegen/init_cpp", tmplData);
  SaveCurrentOutData(fmt::format("{}_init.cpp", projectName));

  // Generate {project}_config.cpp
  REXCODEGEN_TRACE("Recompile: generating {}_config.cpp (PPCImageConfig)", projectName);
  out = renderWithJson(registry, "codegen/config_cpp", tmplData);
  SaveCurrentOutData(fmt::format("{}_config.cpp", projectName));

  // Filter out imports and rexcrt functions before recompilation
  std::erase_if(functions, [](const FunctionNode* fn) {
    return fn->authority() == FunctionAuthority::IMPORT;
  });
  std::erase_if(functions, [&rexcrtByAddr](const FunctionNode* fn) {
    return rexcrtByAddr.contains(static_cast<uint32_t>(fn->base()));
  });

  // Build EmitContext -- resolver is now properly connected
  EmitContext emitCtx{binary(), config(), graph(),
                      static_cast<uint32_t>(analysisState().entryPoint), nullptr};
  if (runtime_)
    emitCtx.resolver = runtime_->export_resolver();

  // Generate recomp files with size-based splitting
  REXCODEGEN_INFO("Recompiling {} functions...", functions.size());
  size_t currentFileBytes = 0;
  println("#include \"{}_init.h\"\n", projectName);

  for (size_t i = 0; i < functions.size(); i++) {
    std::string code = functions[i]->emitCpp(emitCtx);

    if (currentFileBytes > 0 && currentFileBytes + code.size() > REXCVAR_GET(max_file_size_bytes)) {
      SaveCurrentOutData();
      println("#include \"{}_init.h\"\n", projectName);
      currentFileBytes = 0;
    }

    if (code.size() > REXCVAR_GET(max_file_size_bytes)) {
      REXCODEGEN_WARN("Function 0x{:08X} is {} bytes, exceeds max_file_size_bytes ({})",
                      functions[i]->base(), code.size(), REXCVAR_GET(max_file_size_bytes));
    }

    out += code;
    currentFileBytes += code.size();
  }

  SaveCurrentOutData();
  REXCODEGEN_INFO("Recompilation complete.");

  // Generate sources.cmake
  REXCODEGEN_TRACE("Recompile: generating sources.cmake");
  {
    auto& recompFiles = tmplData["recomp_files"];
    recompFiles = nlohmann::json::array();
    for (size_t i = 0; i < cppFileIndex; ++i) {
      recompFiles.push_back(fmt::format("{}_recomp.{}.cpp", projectName, i));
    }
    out = renderWithJson(registry, "codegen/sources_cmake", tmplData);
    SaveCurrentOutData("sources.cmake");
  }

  // Write all buffered files to disk
  FlushPendingWrites();
  return true;
}

void CodegenWriter::SaveCurrentOutData(const std::string_view name) {
  if (!out.empty()) {
    std::string filename;

    if (name.empty()) {
      filename = fmt::format("{}_recomp.{}.cpp", config().projectName, cppFileIndex);
      ++cppFileIndex;
    } else {
      filename = std::string(name);
    }

    pendingWrites.emplace_back(std::move(filename), std::move(out));
    out.clear();
  }
}

void CodegenWriter::FlushPendingWrites() {
  std::filesystem::path outputPath = ctx_.configDir() / config().outDirectoryPath;

  for (const auto& [filename, content] : pendingWrites) {
    std::string filePath = (outputPath / filename).string();
    REXCODEGEN_TRACE("flush_pending_writes: filePath={}", filePath);

    bool shouldWrite = true;

    FILE* f = fopen(filePath.c_str(), "rb");
    if (f) {
      std::vector<uint8_t> temp;

      fseek(f, 0, SEEK_END);
      long fileSize = ftell(f);
      if (fileSize == static_cast<long>(content.size())) {
        fseek(f, 0, SEEK_SET);
        temp.resize(fileSize);
        fread(temp.data(), 1, fileSize, f);

        shouldWrite = !XXH128_isEqual(XXH3_128bits(temp.data(), temp.size()),
                                      XXH3_128bits(content.data(), content.size()));
      }
      fclose(f);
    }

    if (shouldWrite) {
      f = fopen(filePath.c_str(), "wb");
      if (!f) {
        REXCODEGEN_ERROR("Failed to open file for writing: {}", filePath);
        continue;
      }
      fwrite(content.data(), 1, content.size(), f);
      fclose(f);
      REXCODEGEN_TRACE("Wrote {} bytes to {}", content.size(), filePath);
    }
  }

  pendingWrites.clear();
}

}  // namespace rex::codegen
