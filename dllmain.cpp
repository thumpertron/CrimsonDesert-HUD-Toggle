// dllmain.cpp

#include "pch.h"

#include <Windows.h>
#include <vector>
#include <unordered_set>
#include <cstdio>
#include <cstdint>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <atomic>



   // SETTINGS //

constexpr uintptr_t STATIC_BASE_OFFSET = 0x5E57C04;
constexpr uintptr_t ROOT_OFFSET_1 = 0x27C;
constexpr int       SEARCH_RANGE = 0x300;
constexpr int       SEARCH_DEPTH = 7;



// UI ELEMENT //

struct BytePattern {
    uint8_t bytes[8];
    bool    wildcard[8];
};

struct AobPattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> wildcard;
};

struct UIElement {
    std::string            name;
    std::string            widgetString;
    int                    key;
    std::vector<uintptr_t> offsets;
    std::vector<uintptr_t> chain;
    AobPattern             aobPattern;
    int                    flagOffset;
    BytePattern            visiblePattern;
    BytePattern            hiddenPattern;
    BytePattern            detectPattern;
    bool                   hasDetectPattern;
    uintptr_t              candidateRoot;
    uintptr_t              address;
    bool                   holdMode;
    bool                   visible;
    bool                   autoHide;
    bool                   initialised;
};

std::vector<UIElement> gElements;

// Scan thread state
std::atomic<bool> gScanning(false);

struct ScanParams {
    uintptr_t hudRoot;
};
ScanParams gScanParams;


// KEY NAME MAP //

std::map<std::string, int> gKeyMap = {
    { "VK_F1",        VK_F1       }, { "VK_F2",        VK_F2     }, { "VK_0",    '0' }, { "VK_A", 'A' }, { "VK_B", 'B' }, { "VK_C", 'C' },
    { "VK_F3",        VK_F3       }, { "VK_F4",        VK_F4     }, { "VK_1",    '1' }, { "VK_D", 'D' }, { "VK_E", 'E' }, { "VK_F", 'F' },
    { "VK_F5",        VK_F5       }, { "VK_F6",        VK_F6     }, { "VK_2",    '2' }, { "VK_G", 'G' }, { "VK_H", 'H' }, { "VK_I", 'I' },
    { "VK_F7",        VK_F7       }, { "VK_F8",        VK_F8     }, { "VK_3",    '3' }, { "VK_J", 'J' }, { "VK_K", 'K' }, { "VK_L", 'L' },
    { "VK_F9",        VK_F9       }, { "VK_F10",       VK_F10    }, { "VK_4",    '4' }, { "VK_M", 'M' }, { "VK_N", 'N' }, { "VK_O", 'O' },
    { "VK_F11",       VK_F11      }, { "VK_F12",       VK_F12    }, { "VK_5",    '5' }, { "VK_P", 'P' }, { "VK_Q", 'Q' }, { "VK_R", 'R' },

    { "VK_HOME",      VK_HOME     }, { "VK_UP",        VK_UP     }, { "VK_6",    '6' }, { "VK_S", 'S' }, { "VK_T", 'T' }, { "VK_U", 'U' },
    { "VK_INSERT",    VK_INSERT   }, { "VK_DOWN",      VK_DOWN   }, { "VK_7",    '7' }, { "VK_V", 'V' }, { "VK_W", 'W' }, { "VK_X", 'X' },
    { "VK_END",       VK_END      }, { "VK_LEFT",      VK_LEFT   }, { "VK_8",    '8' }, { "VK_Y", 'Y' }, { "VK_Z", 'Z' },
    { "VK_DELETE",    VK_DELETE   }, { "VK_RIGHT",     VK_RIGHT  }, { "VK_9",    '9' },

    { "VK_PRIOR",     VK_PRIOR    }, { "VK_TAB",       VK_TAB    }, { "VK_LCONTROL",  VK_LCONTROL },
    { "VK_NEXT",      VK_NEXT     }, { "VK_CAPITAL",   VK_CAPITAL}, { "VK_LSHIFT",    VK_LSHIFT   },
    { "VK_LMENU",     VK_LMENU    },
};


// PARSE HELPERS //

BytePattern ParsePattern(const std::string& str) {
    BytePattern p{};
    std::istringstream ss(str);
    std::string token;
    int i = 0;
    while (ss >> token && i < 8) {
        if (token == "??") {
            p.wildcard[i] = true;
            p.bytes[i] = 0x00;
        }
        else {
            p.wildcard[i] = false;
            p.bytes[i] = (uint8_t)strtoul(token.c_str(), nullptr, 16);
        }
        i++;
    }
    return p;
}

AobPattern ParseAobPattern(const std::string& str) {
    AobPattern p;
    std::istringstream ss(str);
    std::string token;
    while (ss >> token) {
        if (token == "??") {
            p.wildcard.push_back(true);
            p.bytes.push_back(0x00);
        }
        else {
            p.wildcard.push_back(false);
            p.bytes.push_back((uint8_t)strtoul(token.c_str(), nullptr, 16));
        }
    }
    return p;
}

int ParseKey(const std::string& str) {
    auto it = gKeyMap.find(str);
    if (it != gKeyMap.end()) return it->second;
    return 0;
}

std::vector<uintptr_t> ParseOffsets(const std::string& str) {
    std::vector<uintptr_t> result;
    std::istringstream ss(str);
    std::string token;
    while (ss >> token)
        result.push_back((uintptr_t)strtoull(token.c_str(), nullptr, 16));
    return result;
}



// LOAD INI //

void LoadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        printf("Could not open %s\n", path.c_str());
        return;
    }

    printf("Loading config: %s\n", path.c_str());

    UIElement current{};
    bool      inSection = false;

    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && isspace((unsigned char)line.front())) line.erase(line.begin());
        while (!line.empty() && isspace((unsigned char)line.back()))  line.pop_back();

        if (line.empty() || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            if (inSection && !current.name.empty())
                gElements.push_back(current);
            current = {};
            current.name = line.substr(1, line.size() - 2);
            current.visible = true;
            current.address = 0;
            current.candidateRoot = 0;
            current.holdMode = true;
            current.flagOffset = 0;
            current.hasDetectPattern = false;
            current.autoHide = true;
            current.initialised = false;
            inSection = true;
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && isspace((unsigned char)key.back()))  key.pop_back();
        while (!val.empty() && isspace((unsigned char)val.front())) val.erase(val.begin());

        if (key == "Key")          current.key = ParseKey(val);
        if (key == "VisibleBytes") current.visiblePattern = ParsePattern(val);
        if (key == "HiddenBytes")  current.hiddenPattern = ParsePattern(val);
        if (key == "Mode")         current.holdMode = (val == "hold");
        if (key == "WidgetString") current.widgetString = val;
        if (key == "Chain")        current.chain = ParseOffsets(val);
        if (key == "Offsets")      current.offsets = ParseOffsets(val);
        if (key == "AobPattern")   current.aobPattern = ParseAobPattern(val);
        if (key == "FlagOffset")   current.flagOffset = (int)strtol(val.c_str(), nullptr, 10);
        if (key == "DetectBytes") { current.detectPattern = ParsePattern(val); current.hasDetectPattern = true; }
        if (key == "Default")      current.autoHide = (val == "hide");
    }

    if (inSection && !current.name.empty())
        gElements.push_back(current);

    printf("Loaded %llu UI elements\n", gElements.size());
    for (auto& e : gElements) {
        printf("  [%s] key=%d mode=%s aob=%s flagOffset=%d\n",
            e.name.c_str(), e.key,
            e.holdMode ? "hold" : "toggle",
            e.aobPattern.bytes.empty() ? "no" : "yes",
            e.flagOffset);
    }
}



// SAFE MEMORY //

bool SafeReadQword(uintptr_t addr, uintptr_t& out) {
    __try { out = *(uintptr_t*)addr; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeReadBytes(uintptr_t addr, void* buffer, size_t size) {
    __try { memcpy(buffer, (void*)addr, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeWriteBytes(uintptr_t addr, void* buffer, size_t size) {
    __try {
        DWORD old;
        VirtualProtect((void*)addr, size, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void*)addr, buffer, size);
        VirtualProtect((void*)addr, size, old, &old);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool TestSignature(uintptr_t addr, UIElement& el);



// HEAP RANGE DISCOVERY //

constexpr uintptr_t HEAP_PROBE_MIN = 0x800000000;    // ignore anything below this
uintptr_t gHeapRangeBase = 0x800000000;               // updated after discovery
uintptr_t gHeapRangeTop = 0x700000000000;            // updated after discovery

void DiscoverHeapRange() {
    AobPattern probe = ParseAobPattern("D0 E0 ?? 40 00 80 ?? ??");

    printf("[HEAP] Starting heap range discovery...\n");

    std::vector<uintptr_t> hits;

    // Retry until we get hits, up to 30 seconds
    for (int attempt = 0; attempt < 30 && hits.empty(); attempt++) {
        if (attempt > 0) {
            printf("[HEAP] No hits yet, retrying...\n");
            Sleep(1000);
        }

        hits.clear();

    }

    struct Region { uint8_t* base; size_t size; };
    std::vector<Region> regions;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0;
    while (VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
        uintptr_t regionBase = (uintptr_t)mbi.BaseAddress;
        uintptr_t regionEnd = regionBase + mbi.RegionSize;

        bool usable = (mbi.State == MEM_COMMIT)
            && (mbi.Type == MEM_PRIVATE)
            && (mbi.Protect & PAGE_READWRITE)
            && !(mbi.Protect & PAGE_GUARD)
            && !(mbi.Protect & PAGE_NOACCESS)
            && regionBase >= HEAP_PROBE_MIN
            && regionBase < 0x700000000000
            && mbi.RegionSize >= 0x1000;

        if (usable)
            regions.push_back({ (uint8_t*)regionBase, mbi.RegionSize });

        if (regionEnd <= addr) break;
        addr = regionEnd;
    }

    const size_t patLen = probe.bytes.size();
    const uint8_t* patBytes = probe.bytes.data();
    const uint8_t* patWild = probe.wildcard.data();

    // Find anchor byte
    int anchorIdx = -1;
    for (int i = 0; i < (int)patLen; i++) {
        if (!patWild[i]) { anchorIdx = i; break; }
    }

    constexpr size_t CHUNK = 0x400000;

    for (auto& reg : regions) {
        uint8_t* base = reg.base;
        size_t   remaining = reg.size;

        while (remaining >= patLen) {
            size_t    chunkSize = min(remaining, CHUNK);
            std::vector<uint8_t> buf(chunkSize);
            SIZE_T    bytesRead = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), base, buf.data(), chunkSize, &bytesRead)
                || bytesRead < patLen) {
                base += chunkSize;
                remaining -= min(remaining, chunkSize);
                continue;
            }

            size_t limit = bytesRead - patLen;
            size_t i = 0;
            while (i <= limit) {
                void* hit = anchorIdx >= 0
                    ? memchr(buf.data() + i + anchorIdx, patBytes[anchorIdx], limit - i + 1)
                    : nullptr;
                if (!hit) break;

                i = (uint8_t*)hit - buf.data() - anchorIdx;
                if (i > limit) break;

                bool match = true;
                for (size_t j = 0; j < patLen; j++) {
                    if (patWild[j]) continue;
                    if (buf[i + j] != patBytes[j]) { match = false; break; }
                }
                if (match)
                    hits.push_back((uintptr_t)(base + i));
                i++;
            }

            base += bytesRead;
            remaining -= bytesRead;
        }
    }

    if (hits.empty()) {
        printf("[HEAP] No probe hits found after retries, using default range\n");
        return;
    }

    // Find the min/max of all hits to establish the heap range
    uintptr_t lo = hits[0], hi = hits[0];
    for (auto h : hits) {
        if (h < lo) lo = h;
        if (h > hi) hi = h;
    }

    // Round down/up to nearest 1GB boundary for a clean range
    constexpr uintptr_t GB = 0x40000000ULL;
    constexpr uintptr_t PAD = 8ULL * GB;   // 8GB padding each side

    gHeapRangeBase = (lo > PAD) ? lo - PAD : 0;
    gHeapRangeTop = hi + PAD;

    // Align to 1GB boundaries
    gHeapRangeBase = (gHeapRangeBase / GB) * GB;
    gHeapRangeTop = ((gHeapRangeTop / GB) + 1) * GB;

    printf("[HEAP] Discovery complete: %llu hits, range 0x%llX - 0x%llX\n",
        hits.size(), gHeapRangeBase, gHeapRangeTop);
}



// AOB SCAN //

void AobScanAll() {
    std::vector<UIElement*> targets;
    for (auto& el : gElements)
        if (!el.address && !el.aobPattern.bytes.empty())
            targets.push_back(&el);

    if (targets.empty()) return;

    struct TargetInfo {
        UIElement* el;
        int        anchorIdx;
        uint8_t    anchorByte;
        TargetInfo(UIElement* e, int idx, uint8_t b) : el(e), anchorIdx(idx), anchorByte(b) {}
    };
    std::vector<TargetInfo> infos;
    for (auto* el : targets) {
        int bestIdx = -1, bestScore = -1;
        for (int i = 0; i < (int)el->aobPattern.bytes.size(); i++) {
            if (el->aobPattern.wildcard[i]) continue;
            uint8_t b = el->aobPattern.bytes[i];
            int score = (b != 0x00 && b != 0xFF && b != 0x01 && b != 0x80) ? 1 : 0;
            if (score > bestScore) { bestScore = score; bestIdx = i; }
        }
        infos.push_back(TargetInfo(el, bestIdx, bestIdx >= 0 ? (uint8_t)el->aobPattern.bytes[bestIdx] : (uint8_t)0));
    }

    struct Region { uint8_t* base; size_t size; };
    std::vector<Region> regions;
    regions.reserve(512);

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0;
    while (VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
        uintptr_t regionBase = (uintptr_t)mbi.BaseAddress;
        uintptr_t regionEnd = regionBase + mbi.RegionSize;

        bool isExeRange = (regionBase >= 0x140000000 && regionEnd <= 0x160000000);
        bool isTooLow = (regionEnd <= gHeapRangeBase);
        bool isTooHigh = (regionBase >= gHeapRangeTop);
        bool isStackRange = (regionBase >= 0x200000000 && regionEnd <= 0x800000000);

        bool readable = !isExeRange && !isTooLow && !isTooHigh && !isStackRange
            && (mbi.Type == MEM_PRIVATE)
            && (mbi.State == MEM_COMMIT)
            && (mbi.Protect & PAGE_READWRITE)
            && !(mbi.Protect & PAGE_GUARD)
            && !(mbi.Protect & PAGE_NOACCESS)
            && mbi.RegionSize >= 0x100000;

        if (readable)
            regions.push_back({ (uint8_t*)regionBase, mbi.RegionSize });

        if (regionEnd <= addr) break;
        addr = regionEnd;
    }

    printf("[AOB] Scanning %llu regions for %llu patterns\n",
        regions.size(), infos.size());

    constexpr size_t CHUNK_SIZE = 0x400000;

    for (auto& reg : regions) {
        bool anyLeft = false;
        for (auto& info : infos)
            if (!info.el->address) { anyLeft = true; break; }
        if (!anyLeft) break;

        uint8_t* base = reg.base;
        size_t   remaining = reg.size;

        while (remaining >= 1) {
            size_t chunkSize = min(remaining, CHUNK_SIZE);
            std::vector<uint8_t> buf(chunkSize);
            SIZE_T bytesRead = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), base, buf.data(), chunkSize, &bytesRead)
                || bytesRead == 0) {
                base += chunkSize;
                remaining -= min(remaining, chunkSize);
                continue;
            }

            for (auto& info : infos) {
                if (info.el->address) continue;

                UIElement& el = *info.el;
                const size_t   patLen = el.aobPattern.bytes.size();
                const uint8_t* patBytes = el.aobPattern.bytes.data();
                const uint8_t* patWild = el.aobPattern.wildcard.data();

                if (bytesRead < patLen) continue;
                size_t limit = bytesRead - patLen;

                if (info.anchorIdx < 0) continue;

                size_t i = 0;
                while (i <= limit) {
                    void* hit = memchr(buf.data() + i + info.anchorIdx,
                        info.anchorByte,
                        limit - i + 1);
                    if (!hit) break;

                    i = (uint8_t*)hit - buf.data() - info.anchorIdx;
                    if (i > limit) break;

                    bool match = true;
                    for (size_t j = 0; j < patLen; j++) {
                        if (patWild[j]) continue;
                        if (buf[i + j] != patBytes[j]) { match = false; break; }
                    }

                    if (match) {
                        uintptr_t sigAddr = (uintptr_t)(base + i);
                        uintptr_t flagAddr = (uintptr_t)((intptr_t)sigAddr + el.flagOffset);

                        if (TestSignature(flagAddr, el)) {
                            el.address = flagAddr;
                            printf("[AOB FOUND] %s at 0x%llX (sig=0x%llX offset=%d)\n",
                                el.name.c_str(), flagAddr, sigAddr, el.flagOffset);
                        }
                        else {
                            uint8_t bytes[8] = {};
                            SafeReadBytes(flagAddr, bytes, 8);
                            printf("[AOB] %s sig at 0x%llX flag mismatch at 0x%llX: "
                                "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                                el.name.c_str(), sigAddr, flagAddr,
                                bytes[0], bytes[1], bytes[2], bytes[3],
                                bytes[4], bytes[5], bytes[6], bytes[7]);
                        }
                        break;
                    }
                    i++;
                }
            }

            base += bytesRead;
            remaining -= bytesRead;
        }
    }
}



//  RESOLVE ELEMENT CHAIN //

uintptr_t ResolveElementChain(const std::vector<uintptr_t>& chain) {
    if (chain.empty()) return 0;

    uintptr_t addr = chain[0];
    printf("[CHAIN] Starting at 0x%llX\n", addr);

    for (size_t i = 1; i < chain.size(); i++) {
        if (i == chain.size() - 1) {
            addr = addr + chain[i];
            printf("[CHAIN] Final address: 0x%llX\n", addr);
            return addr;
        }

        uintptr_t next = 0;
        if (!SafeReadQword(addr + chain[i], next) || !next) {
            printf("[CHAIN] Failed at step %llu\n", i);
            return 0;
        }

        printf("[CHAIN] Step %llu: [0x%llX + 0x%llX] -> 0x%llX\n",
            i, addr, chain[i], next);
        addr = next;
    }

    return addr;
}



// HEAP FILTER //

bool IsHeapLike(uintptr_t addr) {
    if (addr < gHeapRangeBase)   return false;
    if (addr >= gHeapRangeTop)   return false;
    if (addr >= 0x140000000 && addr < 0x150000000) return false;
    return true;
}



// WIDGET TEST //

bool LooksLikeWidget(uintptr_t addr) {
    int valid = 0;
    for (uintptr_t offset = 0; offset <= 0x40; offset += 8) {
        uintptr_t ptr = 0;
        if (!SafeReadQword(addr + offset, ptr)) continue;
        if (ptr > 0x100000000 && ptr < 0x7FFFFFFFFFFF && IsHeapLike(ptr))
            valid++;
    }
    return valid >= 2;
}



// PATTERN MATCH //

bool MatchPattern(uintptr_t addr, const BytePattern& pattern) {
    uint8_t bytes[8];
    if (!SafeReadBytes(addr, bytes, 8)) return false;
    for (int i = 0; i < 8; i++) {
        if (pattern.wildcard[i]) continue;
        if (bytes[i] != pattern.bytes[i]) return false;
    }
    return true;
}

bool TestSignature(uintptr_t addr, UIElement& el) {
    if (!IsHeapLike(addr)) return false;

    if (el.hasDetectPattern)
        return MatchPattern(addr, el.detectPattern);

    if (MatchPattern(addr, el.visiblePattern)) return true;
    if (MatchPattern(addr, el.hiddenPattern))  return true;

    return false;
}



// FIND VISIBILITY //

uintptr_t FindVisibilityNear(uintptr_t root, UIElement& el) {
    if (!el.offsets.empty()) {
        for (uintptr_t offset : el.offsets) {
            uintptr_t addr = root + offset;
            if (TestSignature(addr, el)) {
                printf("[OFFSET] %s matched at root+0x%llX\n",
                    el.name.c_str(), offset);
                return addr;
            }
        }
        return 0;
    }

    constexpr int LOCAL_RADIUS = 0x150;
    uintptr_t start = (root > LOCAL_RADIUS) ? root - LOCAL_RADIUS : root;
    uintptr_t end = root + LOCAL_RADIUS;
    for (uintptr_t addr = start; addr < end; addr += 8)
        if (TestSignature(addr, el)) return addr;

    return 0;
}



// STRING MATCH //

uintptr_t FindWidgetStringNode(uintptr_t root, const std::string& target) {
    const char* targetStr = target.c_str();
    size_t      targetLen = target.length();

    for (uintptr_t offset = 0; offset < SEARCH_RANGE; offset += 8) {
        uintptr_t ptr = 0;
        if (!SafeReadQword(root + offset, ptr)) continue;
        if (ptr < 0x100000000 || ptr > 0x7FFFFFFFFFFF) continue;

        bool match = true;
        __try {
            const char* str = (const char*)ptr;
            for (size_t i = 0; i < targetLen; i++) {
                char c = str[i];
                if (c != targetStr[i] || c < 32 || c > 126) { match = false; break; }
            }
            if (match && str[targetLen] != 0) match = false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { match = false; }

        if (match) return ptr;
    }
    return 0;
}



// RECURSIVE SCAN //

std::unordered_set<uintptr_t> gVisited;

void RecursiveScan(uintptr_t root, int depth) {
    if (depth > SEARCH_DEPTH) return;
    if (gVisited.count(root)) return;
    gVisited.insert(root);

    if (depth > 0 && !LooksLikeWidget(root)) return;

    for (auto& el : gElements) {
        if (el.address) continue;
        if (el.widgetString.empty()) continue;

        uintptr_t stringNode = FindWidgetStringNode(root, el.widgetString);
        if (!stringNode) continue;

        if (!el.candidateRoot) {
            printf("[WIDGET] %s string found\n", el.name.c_str());
            el.candidateRoot = root;
        }

        uintptr_t vis = FindVisibilityNear(root, el);
        if (vis) {
            el.address = vis;
            printf("[FOUND] %s visibility at %p\n", el.name.c_str(), (void*)vis);
        }
    }

    for (uintptr_t offset = 0; offset < SEARCH_RANGE; offset += 8) {
        uintptr_t child = 0;
        if (!SafeReadQword(root + offset, child)) continue;
        if (child > 0x100000000 && child < 0x7FFFFFFFFFFF && IsHeapLike(child))
            RecursiveScan(child, depth + 1);
    }
}



// SET ELEMENT VISIBLE //

void SetElementVisible(UIElement& el, bool visible) {
    if (!el.address) return;

    uint8_t bytes[8];
    if (!SafeReadBytes(el.address, bytes, 8)) {
        el.address = 0;
        return;
    }

    const BytePattern& pat = visible ? el.visiblePattern : el.hiddenPattern;
    for (int i = 0; i < 8; i++)
        if (!pat.wildcard[i]) bytes[i] = pat.bytes[i];

    SafeWriteBytes(el.address, bytes, 8);
}



// ELEMENT ADDRESS //

void TryFindElement(UIElement& el) {
    if (el.address) return;

    // Method 1: explicit chain
    if (!el.chain.empty()) {
        uintptr_t chainRoot = ResolveElementChain(el.chain);
        if (chainRoot) {
            uintptr_t vis = FindVisibilityNear(chainRoot, el);
            if (vis) {
                el.address = vis;
                printf("[CHAIN FOUND] %s at %p\n", el.name.c_str(), (void*)vis);
                return;
            }
        }
    }

    // Method 2: widget string scan (handled by RecursiveScan)
}



// SCAN THREAD //

DWORD WINAPI ScanThread(LPVOID) {
    AobScanAll();

    for (auto& el : gElements)
        TryFindElement(el);

    // Widget string scan for anything still missing
    bool needScan = false;
    for (auto& el : gElements)
        if (!el.address && !el.widgetString.empty()) { needScan = true; break; }

    if (needScan) {
        gVisited.clear();
        RecursiveScan(gScanParams.hudRoot, 0);

        for (auto& el : gElements) {
            if (el.address) continue;
            if (!el.candidateRoot) continue;
            uintptr_t vis = FindVisibilityNear(el.candidateRoot, el);
            if (vis) {
                el.address = vis;
                printf("[FOUND] %s at %p\n", el.name.c_str(), (void*)vis);
            }
        }
    }

    // Auto-hide/show newly found elements
    for (auto& el : gElements) {
        if (!el.address) continue;
        if (el.initialised) continue;  // already processed, skip

        if (el.autoHide) {
            el.visible = false;
            SetElementVisible(el, false);
            printf("[AUTO-HIDE] %s\n", el.name.c_str());
        }
        else {
            el.visible = true;
            SetElementVisible(el, true);
            printf("[AUTO-SHOW] %s\n", el.name.c_str());
        }
        el.initialised = true;
    }

    gScanning = false;
    return 0;
}



// MAIN THREAD //

DWORD WINAPI ModThread(LPVOID) {
    uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
    if (moduleBase != 0x140000000) return 0;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string logDir(exePath);
    logDir = logDir.substr(0, logDir.find_last_of("\\/"));
    std::string logPath = logDir + "\\HUD_Toggle.log";

    FILE* f;
    freopen_s(&f, logPath.c_str(), "w", stdout);
    setvbuf(stdout, nullptr, _IONBF, 0);

    printf("================================\n");
    printf("Crimson Desert HUD Toggle Mod\n");
    printf("================================\n\n");

    DiscoverHeapRange();

    LoadConfig(logDir + "\\HUD_Toggle.ini");

    if (gElements.empty()) {
        printf("No UI elements defined in HUD_Toggle.ini\n");
        return 0;
    }

    uintptr_t staticBase = moduleBase + STATIC_BASE_OFFSET;
    printf("ModuleBase = %p\n", (void*)moduleBase);
    printf("StaticBase = %p\n\n", (void*)staticBase);

    uintptr_t hudRoot = 0;
    while (!hudRoot) {
        SafeReadQword(staticBase + ROOT_OFFSET_1, hudRoot);
        Sleep(1000);
    }
    printf("HUDRoot = %p\n\n", (void*)hudRoot);

    printf("\nControls:\n");
    for (auto& el : gElements)
        printf("  [%s] key=%d mode=%s\n",
            el.name.c_str(), el.key,
            el.holdMode ? "hold" : "toggle");
    printf("\n");

    std::vector<bool> keyHeld(gElements.size(), false);



    // Main loop //

    while (true) {
       
        bool anyUnresolved = false;
        for (auto& el : gElements)
            if (!el.address) { anyUnresolved = true; break; }

        if (anyUnresolved && !gScanning) {
            gScanning = true;
            gScanParams.hudRoot = hudRoot;
            CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
        }

        // Validate existing addresses
        for (auto& el : gElements) {
            if (!el.address) continue;
            uint8_t test[8];
            if (!SafeReadBytes(el.address, test, 8)) {
                printf("%s invalidated\n", el.name.c_str());
                el.address = 0;
                el.candidateRoot = 0;
                el.initialised = false;
            }
        }

        // Hold / toggle input
        for (size_t i = 0; i < gElements.size(); i++) {
            auto& el = gElements[i];
            if (!el.key || !el.address) continue;

            bool isDown = (GetAsyncKeyState(el.key) & 0x8000) != 0;

            if (el.holdMode) {
                if (isDown && !keyHeld[i]) {
                    keyHeld[i] = true;
                    el.visible = true;
                    SetElementVisible(el, true);
                    printf("%s shown (holding)\n", el.name.c_str());
                }
                else if (!isDown && keyHeld[i]) {
                    keyHeld[i] = false;
                    el.visible = false;
                    SetElementVisible(el, false);
                    printf("%s hidden (released)\n", el.name.c_str());
                }
            }
            else {
                if (isDown && !keyHeld[i]) {
                    keyHeld[i] = true;
                    el.visible = !el.visible;
                    SetElementVisible(el, el.visible);
                    printf("%s %s (toggle)\n",
                        el.name.c_str(),
                        el.visible ? "shown" : "hidden");
                }
                else if (!isDown) {
                    keyHeld[i] = false;
                }
            }
        }

        Sleep(50);
    }

    return 0;
}



// DLL ENTRY //

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, ModThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
