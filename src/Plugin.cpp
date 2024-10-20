using namespace SKSE;
using namespace SKSE::log;
using namespace SKSE::stl;

#include "Plugin.h"
#include "GameEventHandler.h"

namespace plugin {
    std::optional<std::filesystem::path> getLogDirectory() {
        using namespace std::filesystem;
        PWSTR buf;
        SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &buf);
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> documentsPath{buf, CoTaskMemFree};
        path directory{documentsPath.get()};
        directory.append("My Games"sv);

        if (exists("steam_api64.dll"sv)) {
            if (exists("openvr_api.dll") || exists("Data/SkyrimVR.esm")) {
                directory.append("Skyrim VR"sv);
            } else {
                directory.append("Skyrim Special Edition"sv);
            }
        } else if (exists("Galaxy64.dll"sv)) {
            directory.append("Skyrim Special Edition GOG"sv);
        } else if (exists("eossdk-win64-shipping.dll"sv)) {
            directory.append("Skyrim Special Edition EPIC"sv);
        } else {
            return current_path().append("skselogs");
        }
        return directory.append("SKSE"sv).make_preferred();
    }

    void initializeLogging() {
        auto path = getLogDirectory();
        if (!path) {
            report_and_fail("Can't find SKSE log directory");
        }
        *path /= std::format("{}.log"sv, Plugin::Name);

        std::shared_ptr<spdlog::logger> log;
        if (IsDebuggerPresent()) {
            log = std::make_shared<spdlog::logger>("Global", std::make_shared<spdlog::sinks::msvc_sink_mt>());
        } else {
            log = std::make_shared<spdlog::logger>("Global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
        }
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern(PLUGIN_LOGPATTERN_DEFAULT);
    }
}  // namespace plugin

using namespace plugin;


template <typename Func>
auto WriteFunctionHook(std::uint64_t id, std::size_t byteCopyCount, Func destination) {
    const REL::Relocation target{REL::ID(id)};
    struct XPatch : Xbyak::CodeGenerator {
            using ull = unsigned long long;
            using uch = unsigned char;
            XPatch(std::uintptr_t originalFuncAddr, ull originalByteLength, ull newByteLength)
                : Xbyak::CodeGenerator(originalByteLength + newByteLength, TRAMPOLINE.allocate(originalByteLength + newByteLength)) {
                auto byteAddr = reinterpret_cast<uch*>(originalFuncAddr);
                for (ull i = 0; i < originalByteLength; i++)
                    db(*byteAddr++);
                jmp(qword[rip]);
                dq(ull(byteAddr));
            }
    };
    XPatch patch(target.address(), byteCopyCount, 20);
    patch.ready();
    auto patchSize = patch.getSize();
    TRAMPOLINE.write_branch<5>(target.address(), destination);
    auto alloc = TRAMPOLINE.allocate(patchSize);
    memcpy(alloc, patch.getCode(), patchSize);
    return reinterpret_cast<std::uintptr_t>(alloc);
}

class PlayerCanActivateFurniture {
    private:
        struct Function {
                static bool Modified(RE::TESFurniture* furn_base, RE::TESObjectREFR* furn_refr, RE::PlayerCharacter* pc) {
                    bool result = Original(furn_base, furn_refr, pc);
                    logs::info("furn_base = 0x{:X}, furn_refr = 0x{:X}, pc = 0x{:X}, result = {}", furn_base->GetFormID(),
                               furn_refr->GetFormID(), pc->GetFormID(), result);
                    /*if (!result && pc->IsInMidair()) {
                        result = true;
                    }*/
                    return result;
                }
                static inline REL::Relocation<decltype(Modified)> Original;
        };

    public:
        static void Install() {
            // mov  rax, rsp  = 3
            // push rdi       = 1
            // sub  rsp, 0B0h = 7
            Function::Original = WriteFunctionHook(REL::RelocationID(17034, 17420, 17034).id(), 11, Function::Modified);
            logs::info("PlayerCanActivateFurniture hook installed");
        }
};
extern "C" DLLEXPORT bool SKSEPlugin_Load(const LoadInterface* skse) {
    initializeLogging();

    logger::info("'{} {}' is loading, game version '{}'...", Plugin::Name, Plugin::VersionString, REL::Module::get().version().string());
    Init(skse);
    SKSE::AllocTrampoline(128);
    PlayerCanActivateFurniture::Install();
    GameEventHandler::getInstance().onLoad();
    logger::info("{} has finished loading.", Plugin::Name);
    return true;
}