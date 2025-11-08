#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/appmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/ctrl.h>
#include <stdio.h>
#include <string>

#include "husarnet/husarnet_manager.h"
// #include "husarnet/dashboardapi/response.h"
#include "debug.h"
#include <sodium/randombytes.h>

extern "C" struct randombytes_implementation randombytes_sysrandom_implementation;

namespace {

int husarnet_thread(SceSize args, void* argp) {
    auto managerPtr = reinterpret_cast<HusarnetManager**>(argp);
    if (managerPtr && *managerPtr) {
        (*managerPtr)->runHusarnet();
    }
    return 0;
}

int wait_for_network() {
    vita_debug_log("Waiting for network connection...");

    constexpr int maxAttempts = 60; // ~30 seconds
    int state = 0;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        int ret = sceNetCtlInetGetState(&state);
        if (ret == 0) {
            vita_debug_log("Network state: %d", state);
            if (state >= SCE_NETCTL_STATE_CONNECTED) {
                SceNetCtlInfo info{};
                if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) == 0) {
                    vita_debug_log("IP address: %s", info.ip_address);
                }
                return 0;
            }
        } else {
            vita_debug_log("sceNetCtlInetGetState failed: 0x%08X", ret);
        }

        sceKernelDelayThread(500 * 1000); // 500 ms
    }

    vita_debug_log("Network not ready after timeout");
    return -1;
}

void wait_for_exit_button() {
    vita_debug_log("Press X to exit the application");
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);

    while (true) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_CROSS) {
            vita_debug_log("Exit button pressed");
            break;
        }
        sceKernelDelayThread(100 * 1000);
    }
}

} // namespace

int main(int argc, char *argv[]) {
    vita_debug_log("\e[2JStarting Husarnet PS Vita");
    randombytes_set_implementation(&randombytes_sysrandom_implementation);
    SceNetInitParam netInitParam{};
    bool netInitialized = false;
    bool netCtlInitialized = false;
    HusarnetManager* manager = nullptr;
    SceUID husarnetThread = -1;
    bool moduleLoaded = false;

    do {
        // Load network module
        int modRet = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
        vita_debug_log("sceSysmoduleLoadModule(SCE_SYSMODULE_NET) -> 0x%08X", modRet);
        if (modRet < 0) {
            wait_for_exit_button();
            break;
        }
        moduleLoaded = true;

        // Initialize Vita network
        netInitParam.memory = malloc(1024 * 1024);
        netInitParam.size = 1024 * 1024;
        netInitParam.flags = 0;

        if (netInitParam.memory == nullptr) {
            vita_debug_log("Failed to allocate network memory");
            wait_for_exit_button();
            break;
        }

        int netInitRet = sceNetInit(&netInitParam);
        vita_debug_log("sceNetInit -> 0x%08X", netInitRet);
        if (netInitRet < 0) {
            wait_for_exit_button();
            break;
        }
        netInitialized = true;

        int ret = sceNetCtlInit();
        vita_debug_log("sceNetCtlInit -> 0x%08X", ret);
        if (ret < 0) {
            wait_for_exit_button();
            break;
        }
        netCtlInitialized = true;

        wait_for_network();

        vita_debug_log("Creating HusarnetManager");
        manager = new HusarnetManager();
        vita_debug_log("HusarnetManager created");

        // Hardcoded join for PS Vita from join.txt
        #ifdef HUSARNET_NETWORK_ID
        std::string networkId = HUSARNET_NETWORK_ID;
        std::string secret = HUSARNET_SECRET;
        std::string hostname = HUSARNET_HOSTNAME;
        std::string joinCode = networkId + "/" + secret;
        vita_debug_log("Joining network: %s as %s", networkId.c_str(), hostname.c_str());
        manager->joinNetwork(joinCode, hostname);
        vita_debug_log("Join completed");
        #else
        vita_debug_log("No join.txt found, skipping join");
        #endif

        vita_debug_log("Husarnet initialized");

        // Run Husarnet
        vita_debug_log("Running Husarnet");

        vita_debug_log("Creating thread");
        husarnetThread = sceKernelCreateThread("husarnet_main", husarnet_thread, 0x10000100, 0x4000, 0, 0, nullptr);
        vita_debug_log("Thread created: %d", husarnetThread);
        if (husarnetThread >= 0) {
            HusarnetManager* managerArg = manager;
            vita_debug_log("Starting thread");
            sceKernelStartThread(husarnetThread, sizeof(managerArg), &managerArg);
            vita_debug_log("Thread started");
        } else {
            vita_debug_log("Failed to start Husarnet thread: 0x%08X", husarnetThread);
        }

        wait_for_exit_button();
    } while (false);

    vita_debug_log("Shutting down");

    if (husarnetThread >= 0) {
        // Note: No direct terminate function available in VitaSDK, thread will be cleaned up on process exit
        sceKernelDeleteThread(husarnetThread);
    }

    if (manager != nullptr) {
        delete manager;
    }

    if (netCtlInitialized) {
        sceNetCtlTerm();
    }
    if (netInitialized) {
        sceNetTerm();
    }
    if (netInitParam.memory) {
        free(netInitParam.memory);
    }
    if (moduleLoaded) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    }
    return 0;
}